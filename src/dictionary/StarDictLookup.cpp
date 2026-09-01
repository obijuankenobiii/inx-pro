#include "StarDictLookup.h"

#include <Arduino.h>
#include <SDCardManager.h>
#include <esp_task_wdt.h>

#include <algorithm>
#include <cctype>

#include "util/SdIoMutex.h"
#include "util/StringUtils.h"

namespace {

constexpr uint32_t kDictionarySeekChunkBytes = 256 * 1024;
constexpr uint32_t kDefinitionReadChunkBytes = 512;

bool seekFileInWatchdogFriendlyChunks(FsFile& file, const uint64_t target) {
  uint64_t current = file.position();
  if (target < current) {
    if (!file.seekSet(0)) {
      return false;
    }
    current = 0;
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(1));
  }

  while (current < target) {
    const uint64_t next = std::min(target, current + static_cast<uint64_t>(kDictionarySeekChunkBytes));
    if (!file.seekSet(next)) {
      return false;
    }
    current = next;
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  return true;
}

uint32_t readBigEndian32(FsFile& f) {
  uint8_t b[4] = {0, 0, 0, 0};
  if (f.read(b, 4) != 4) {
    return 0;
  }
  return (static_cast<uint32_t>(b[0]) << 24) | (static_cast<uint32_t>(b[1]) << 16) |
         (static_cast<uint32_t>(b[2]) << 8) | static_cast<uint32_t>(b[3]);
}

uint64_t readBigEndian64(FsFile& f) {
  uint8_t b[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  if (f.read(b, 8) != 8) {
    return 0;
  }
  uint64_t v = 0;
  for (int i = 0; i < 8; ++i) {
    v = (v << 8) | static_cast<uint64_t>(b[i]);
  }
  return v;
}

/** Reads a null-terminated word from f into out. Returns false on EOF before a terminator or if
 *  the word looks corrupt (unreasonably long, which would otherwise scan indefinitely). */
bool readCString(FsFile& f, std::string& out) {
  out.clear();
  char c = 0;
  while (true) {
    if (f.read(&c, 1) != 1) {
      return false;
    }
    if (c == '\0') {
      return true;
    }
    out += c;
    if (out.size() > 256) {
      return false;
    }
  }
}

std::string toLowerCopy(const std::string& s) {
  std::string out = s;
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return std::tolower(c); });
  return out;
}

std::string toTitleCaseCopy(const std::string& s) {
  std::string out = toLowerCopy(s);
  if (!out.empty()) {
    out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
  }
  return out;
}

/** Strips leading/trailing characters that aren't letters/digits/apostrophe/hyphen, so a word
 *  lifted straight from rendered book text (with trailing commas/periods/quotes) can still match
 *  a dictionary entry. */
std::string stripPunctuation(const std::string& s) {
  size_t start = 0;
  size_t end = s.size();
  auto keep = [](unsigned char c) { return std::isalnum(c) || c == '\'' || c == '-'; };
  while (start < end && !keep(static_cast<unsigned char>(s[start]))) {
    ++start;
  }
  while (end > start && !keep(static_cast<unsigned char>(s[end - 1]))) {
    --end;
  }
  return s.substr(start, end - start);
}

/** Generates candidate base forms for a possibly-inflected English word (possessive, plural,
 *  past tense -ed, gerund -ing), so a form absent from the dictionary (e.g. "running", "jumped",
 *  "books") can still resolve to its base entry ("run", "jump", "book"). Heuristic suffix-stripping,
 *  not a full stemmer - callers try each candidate as an exact lookup and take the first hit, so
 *  over-generating (a few wrong candidates) is harmless as long as the right one is in the list. */
std::vector<std::string> stemCandidates(const std::string& lower) {
  std::vector<std::string> out;
  const size_t n = lower.size();
  auto add = [&](const std::string& s) {
    if (s.size() >= 2) {
      out.push_back(s);
    }
  };

  if (n > 2 && lower[n - 2] == '\'' && lower[n - 1] == 's') {
    add(lower.substr(0, n - 2));
  }

  if (n > 4 && lower.compare(n - 3, 3, "ing") == 0) {
    const std::string base = lower.substr(0, n - 3);
    add(base);
    add(base + "e");
    if (base.size() >= 3 && base[base.size() - 1] == base[base.size() - 2]) {
      add(base.substr(0, base.size() - 1));
    }
  }

  if (n > 3 && lower.compare(n - 3, 3, "ied") == 0) {
    add(lower.substr(0, n - 3) + "y");
  }

  if (n > 3 && lower.compare(n - 2, 2, "ed") == 0) {
    const std::string base = lower.substr(0, n - 2);
    add(base);
    add(base + "e");
    if (base.size() >= 3 && base[base.size() - 1] == base[base.size() - 2]) {
      add(base.substr(0, base.size() - 1));
    }
  }

  if (n > 4 && lower.compare(n - 3, 3, "ies") == 0) {
    add(lower.substr(0, n - 3) + "y");
  }

  if (n > 3 && lower.compare(n - 2, 2, "es") == 0) {
    add(lower.substr(0, n - 2));
  }

  if (n > 2 && lower[n - 1] == 's' && lower[n - 2] != 's') {
    add(lower.substr(0, n - 1));
  }

  return out;
}

}

int StarDictLookup::compareWord(const std::string& a, const std::string& b) {
  return a.compare(b);
}

void StarDictLookup::close() {
  SdIoMutex::Lock lock;
  if (idxFile_) {
    idxFile_.close();
  }
  if (dictFile_) {
    dictFile_.close();
  }
  std::vector<Checkpoint>().swap(checkpoints_);
  std::string().swap(bookname_);
  std::string().swap(sameTypeSequence_);
  wordCount_ = 0;
  idxFileSize_ = 0;
  use64BitOffsets_ = false;
  sorted_ = true;
  foldedSorted_ = true;
  isOpen_ = false;
}

bool StarDictLookup::parseIfo(const std::string& ifoPath) {
  const String contents = SdMan.readFile(ifoPath.c_str());
  if (contents.isEmpty()) {
    return false;
  }

  int lineStart = 0;
  const int len = contents.length();
  while (lineStart < len) {
    int lineEnd = contents.indexOf('\n', lineStart);
    if (lineEnd < 0) {
      lineEnd = len;
    }
    String line = contents.substring(lineStart, lineEnd);
    line.trim();
    lineStart = lineEnd + 1;

    const int eq = line.indexOf('=');
    if (eq <= 0) {
      continue;
    }
    const String key = line.substring(0, eq);
    String value = line.substring(eq + 1);
    value.trim();

    if (key == "bookname") {
      bookname_ = value.c_str();
    } else if (key == "wordcount") {
      wordCount_ = static_cast<uint32_t>(value.toInt());
    } else if (key == "idxfilesize") {
      idxFileSize_ = static_cast<uint32_t>(value.toInt());
    } else if (key == "sametypesequence") {
      sameTypeSequence_ = value.c_str();
    } else if (key == "idxoffsetbits") {
      use64BitOffsets_ = (value.toInt() == 64);
    }
  }

  return true;
}

bool StarDictLookup::open(const std::string& folderPath) {
  close();
  SdIoMutex::Lock lock;

  std::string ifoPath, idxPath, dictPath;
  for (const String& name : SdMan.listFiles(folderPath.c_str())) {
    if (name.length() > 0 && name[0] == '.') {
      continue;
    }
    const std::string full = folderPath + "/" + name.c_str();
    if (StringUtils::checkFileExtension(name, ".ifo")) {
      ifoPath = full;
    } else if (StringUtils::checkFileExtension(name, ".idx")) {
      idxPath = full;
    } else if (StringUtils::checkFileExtension(name, ".dict")) {
      dictPath = full;
    }
  }

  if (ifoPath.empty() || idxPath.empty() || dictPath.empty()) {
    INX_SERIAL.printf("[%lu] [DICT] Missing .ifo/.idx/.dict under %s (ifo='%s' idx='%s' dict='%s')\n", millis(),
                  folderPath.c_str(), ifoPath.c_str(), idxPath.c_str(), dictPath.c_str());
    return false;
  }
  INX_SERIAL.printf("[%lu] [DICT] Found ifo='%s' idx='%s' dict='%s'\n", millis(), ifoPath.c_str(), idxPath.c_str(),
                dictPath.c_str());

  if (!parseIfo(ifoPath)) {
    INX_SERIAL.printf("[%lu] [DICT] Could not parse %s\n", millis(), ifoPath.c_str());
    return false;
  }
  INX_SERIAL.printf("[%lu] [DICT] .ifo says bookname='%s' wordcount=%u idxfilesize=%u sametypesequence='%s' "
                "idxoffsetbits64=%d\n",
                millis(), bookname_.c_str(), wordCount_, idxFileSize_, sameTypeSequence_.c_str(),
                use64BitOffsets_ ? 1 : 0);

  if (!SdMan.openFileForRead("DICT", idxPath, idxFile_) || !SdMan.openFileForRead("DICT", dictPath, dictFile_)) {
    INX_SERIAL.printf("[%lu] [DICT] Could not open .idx/.dict under %s\n", millis(), folderPath.c_str());
    close();
    return false;
  }

  const uint32_t actualIdxSize = static_cast<uint32_t>(idxFile_.fileSize());
  if (idxFileSize_ != actualIdxSize) {
    INX_SERIAL.printf("[%lu] [DICT] .ifo idxfilesize=%u does not match actual .idx size=%u - using actual\n", millis(),
                  idxFileSize_, actualIdxSize);
  }
  idxFileSize_ = actualIdxSize;
  INX_SERIAL.printf("[%lu] [DICT] .idx actual size=%u .dict actual size=%llu\n", millis(), idxFileSize_,
                static_cast<unsigned long long>(dictFile_.fileSize()));

  if (!buildCheckpoints()) {
    INX_SERIAL.printf("[%lu] [DICT] Could not build index checkpoints for %s\n", millis(), folderPath.c_str());
    close();
    return false;
  }

  isOpen_ = true;
  INX_SERIAL.printf("[%lu] [DICT] Opened '%s' (%u words, %u checkpoints, %s offsets, sort=%s)\n", millis(),
                bookname_.c_str(), wordCount_, static_cast<unsigned>(checkpoints_.size()),
                use64BitOffsets_ ? "64-bit" : "32-bit",
                foldedSorted_ ? "case-folded" : (sorted_ ? "byte" : "unsorted"));
  return true;
}

bool StarDictLookup::readIdxEntryAt(const uint32_t idxOffset, std::string& outEntryText, uint64_t& outDictOffset,
                                    uint32_t& outDictSize, uint32_t& outNextOffset) {
  if (!idxFile_.seekSet(idxOffset)) {
    return false;
  }
  return readIdxEntry(outEntryText, outDictOffset, outDictSize, outNextOffset);
}

bool StarDictLookup::readIdxEntry(std::string& outEntryText, uint64_t& outDictOffset, uint32_t& outDictSize,
                                  uint32_t& outNextOffset) {
  if (!readCString(idxFile_, outEntryText)) {
    return false;
  }
  outDictOffset = use64BitOffsets_ ? readBigEndian64(idxFile_) : static_cast<uint64_t>(readBigEndian32(idxFile_));
  outDictSize = readBigEndian32(idxFile_);
  outNextOffset = static_cast<uint32_t>(idxFile_.position());
  return true;
}

bool StarDictLookup::buildCheckpoints() {
  checkpoints_.clear();
  checkpoints_.reserve((wordCount_ + kCheckpointStride - 1) / kCheckpointStride);
  sorted_ = true;
  foldedSorted_ = true;
  if (!idxFile_.seekSet(0)) {
    return false;
  }

  uint32_t offset = 0;
  uint32_t count = 0;
  std::string previous;
  std::string previousFolded;
  while (offset < idxFileSize_) {
    std::string entryText;
    uint64_t dictOffset = 0;
    uint32_t dictSize = 0, nextOffset = 0;
    if (!readIdxEntry(entryText, dictOffset, dictSize, nextOffset)) {
      INX_SERIAL.printf("[%lu] [DICT] buildCheckpoints: read failed at offset=%u after %u entries (idxFileSize=%u)\n",
                    millis(), offset, count, idxFileSize_);
      break;
    }
    const std::string folded = toLowerCopy(entryText);
    if (count > 0) {
      sorted_ = sorted_ && compareWord(previous, entryText) <= 0;
      foldedSorted_ = foldedSorted_ && compareWord(previousFolded, folded) <= 0;
    }
    if (count % kCheckpointStride == 0) {
      checkpoints_.push_back(Checkpoint{offset, entryText});
      if (checkpoints_.size() <= 3) {
        INX_SERIAL.printf("[%lu] [DICT] checkpoint #%u @offset=%u entry='%s'\n", millis(),
                      static_cast<unsigned>(checkpoints_.size() - 1), offset, entryText.c_str());
      }
    }
    previous = std::move(entryText);
    previousFolded = folded;
    ++count;
    if ((count & 0x3F) == 0) {
      esp_task_wdt_reset();
      vTaskDelay(pdMS_TO_TICKS(1));
    }
    if (nextOffset <= offset) {
      INX_SERIAL.printf("[%lu] [DICT] buildCheckpoints: non-advancing entry at offset=%u ('%s') - stopping\n", millis(),
                    offset, entryText.c_str());
      break;
    }
    offset = nextOffset;
  }
  INX_SERIAL.printf("[%lu] [DICT] buildCheckpoints: scanned %u entries total (.ifo wordcount=%u), %u checkpoints, "
                "sort=%s, last checkpoint entry='%s'\n",
                millis(), count, wordCount_, static_cast<unsigned>(checkpoints_.size()),
                foldedSorted_ ? "case-folded" : (sorted_ ? "byte" : "unsorted"),
                checkpoints_.empty() ? "" : checkpoints_.back().entryText.c_str());
  return !checkpoints_.empty();
}

bool StarDictLookup::lookupViaCheckpoints(const std::string& candidate, const bool folded, uint64_t& outDictOffset,
                                          uint32_t& outDictSize) {
  if (checkpoints_.empty() || (folded ? !foldedSorted_ : !sorted_)) {
    return false;
  }
  size_t lo = 0, hi = checkpoints_.size();
  while (lo < hi) {
    const size_t mid = lo + (hi - lo) / 2;
    const std::string checkpoint = folded ? toLowerCopy(checkpoints_[mid].entryText) : checkpoints_[mid].entryText;
    if (compareWord(checkpoint, candidate) <= 0) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  if (lo == 0) {
    return false;
  }
  const uint32_t scanStart = checkpoints_[lo - 1].idxOffset;
  const uint32_t scanEnd = (lo < checkpoints_.size()) ? checkpoints_[lo].idxOffset : idxFileSize_;

  if (!idxFile_.seekSet(scanStart)) {
    return false;
  }
  uint32_t offset = scanStart;
  uint32_t count = 0;
  while (offset < scanEnd) {
    std::string entryWord;
    uint64_t dictOffset = 0;
    uint32_t dictSize = 0, nextOffset = 0;
    if (!readIdxEntry(entryWord, dictOffset, dictSize, nextOffset)) {
      break;
    }
    ++count;
    if ((count & 0x3F) == 0) {
      esp_task_wdt_reset();
      vTaskDelay(pdMS_TO_TICKS(1));
    }
    const std::string value = folded ? toLowerCopy(entryWord) : entryWord;
    const int cmp = compareWord(value, candidate);
    if (cmp == 0) {
      outDictOffset = dictOffset;
      outDictSize = dictSize;
      return true;
    }
    if (cmp > 0) {
      break;
    }
    if (nextOffset <= offset) {
      break;
    }
    offset = nextOffset;
  }
  return false;
}

bool StarDictLookup::lookupViaLinearScan(const std::vector<std::string>& candidates, uint64_t& outDictOffset,
                                         uint32_t& outDictSize, std::string& matched) {
  if (!idxFile_.seekSet(0)) {
    return false;
  }
  uint32_t offset = 0;
  uint32_t count = 0;
  while (offset < idxFileSize_) {
    std::string entryWord;
    uint64_t dictOffset = 0;
    uint32_t dictSize = 0, nextOffset = 0;
    if (!readIdxEntry(entryWord, dictOffset, dictSize, nextOffset)) {
      break;
    }
    ++count;
    if ((count & 0x3F) == 0) {
      esp_task_wdt_reset();
      vTaskDelay(pdMS_TO_TICKS(1));
    }
    const std::string entryLower = toLowerCopy(entryWord);
    for (const std::string& candidate : candidates) {
      if (entryLower == candidate) {
        outDictOffset = dictOffset;
        outDictSize = dictSize;
        matched = candidate;
        return true;
      }
    }
    if (nextOffset <= offset) {
      break;
    }
    offset = nextOffset;
  }
  return false;
}

bool StarDictLookup::lookup(const std::string& queryWord, std::string& outDefinition, bool* outTruncated) {
  SdIoMutex::Lock lock;
  if (!isOpen_) {
    INX_SERIAL.printf("[%lu] [DICT] lookup('%s'): dictionary not open\n", millis(), queryWord.c_str());
    return false;
  }

  const std::string cleaned = stripPunctuation(queryWord);
  if (cleaned.empty()) {
    INX_SERIAL.printf("[%lu] [DICT] lookup('%s'): empty after stripPunctuation\n", millis(), queryWord.c_str());
    return false;
  }

  uint64_t dictOffset = 0;
  uint32_t dictSize = 0;
  bool found = false;

  const std::string lowerCleaned = toLowerCopy(cleaned);

  std::vector<std::string> candidates = {cleaned, lowerCleaned, toTitleCaseCopy(cleaned)};
  std::vector<std::string> foldedCandidates = {lowerCleaned};
  for (const std::string& stem : stemCandidates(lowerCleaned)) {
    candidates.push_back(stem);
    candidates.push_back(toTitleCaseCopy(stem));
    foldedCandidates.push_back(stem);
  }

  const unsigned long t0 = millis();
  std::string hitCandidate;
  const std::vector<std::string>& fastCandidates = foldedSorted_ ? foldedCandidates : candidates;
  for (const std::string& candidate : fastCandidates) {
    if (lookupViaCheckpoints(candidate, foldedSorted_, dictOffset, dictSize)) {
      found = true;
      hitCandidate = candidate;
      INX_SERIAL.printf("[%lu] [DICT] lookup('%s'): fast path hit on candidate='%s' (%lums)\n", millis(),
                    queryWord.c_str(), candidate.c_str(), millis() - t0);
      break;
    }
  }

  if (!found) {
    INX_SERIAL.printf("[%lu] [DICT] lookup('%s'): fast path missed (%lums), falling back to linear scan over %u "
                  "idx bytes\n",
                  millis(), queryWord.c_str(), millis() - t0, idxFileSize_);
    const unsigned long t1 = millis();
    found = lookupViaLinearScan(foldedCandidates, dictOffset, dictSize, hitCandidate);
    INX_SERIAL.printf("[%lu] [DICT] lookup('%s'): linear scan %s (%lums)\n", millis(), queryWord.c_str(),
                  found ? "hit" : "miss", millis() - t1);
  }

  if (!found) {
    return false;
  }

  INX_SERIAL.printf("[%lu] [DICT] lookup('%s'): matched '%s', dictOffset=%llu dictSize=%u\n", millis(),
                queryWord.c_str(), hitCandidate.c_str(), static_cast<unsigned long long>(dictOffset), dictSize);

  INX_SERIAL.printf("[%lu] [DICT] lookup('%s'): seeking dict offset=%llu from=%llu\n", millis(), queryWord.c_str(),
                static_cast<unsigned long long>(dictOffset), static_cast<unsigned long long>(dictFile_.position()));
  if (dictSize == 0 || !seekFileInWatchdogFriendlyChunks(dictFile_, dictOffset)) {
    INX_SERIAL.printf("[%lu] [DICT] lookup('%s'): dictSize==0 or chunked seekSet(%llu) failed\n", millis(),
                  queryWord.c_str(), static_cast<unsigned long long>(dictOffset));
    return false;
  }
  INX_SERIAL.printf("[%lu] [DICT] lookup('%s'): dict seek complete position=%llu\n", millis(), queryWord.c_str(),
                static_cast<unsigned long long>(dictFile_.position()));
  const uint32_t readSize = std::min(dictSize, kMaxDefinitionBytes);
  if (outTruncated) {
    *outTruncated = readSize < dictSize;
  }
  outDefinition.resize(readSize);
  uint32_t totalRead = 0;
  while (totalRead < readSize) {
    const uint32_t chunkSize = std::min(kDefinitionReadChunkBytes, readSize - totalRead);
    const int readN = dictFile_.read(&outDefinition[totalRead], chunkSize);
    if (readN != static_cast<int>(chunkSize)) {
      INX_SERIAL.printf("[%lu] [DICT] lookup('%s'): read %u of %u expected bytes\n", millis(), queryWord.c_str(),
                    totalRead + (readN > 0 ? static_cast<uint32_t>(readN) : 0), readSize);
      return false;
    }
    totalRead += static_cast<uint32_t>(readN);
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  INX_SERIAL.printf("[%lu] [DICT] lookup('%s'): definition read complete bytes=%u truncated=%d\n", millis(),
                queryWord.c_str(), readSize, outTruncated && *outTruncated ? 1 : 0);
  if (totalRead != readSize) {
    INX_SERIAL.printf("[%lu] [DICT] lookup('%s'): read %u of %u expected bytes\n", millis(), queryWord.c_str(),
                  totalRead, readSize);
    return false;
  }
  return true;
}
