/**
 * @file PdfDocument.cpp
 * @brief Definitions for PdfDocument.
 */

#include "PdfDocument.h"

#include <HardwareSerial.h>
#include <InflateReader.h>
#include <SDCardManager.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>
#include <cstring>

#include "PdfLexer.h"
#include "PdfValueParser.h"

namespace {

// Finds the last occurrence of `needle` in [data, data+size). Returns size on failure.
size_t findLastOccurrence(const uint8_t* data, size_t size, const char* needle) {
  const size_t needleLen = std::strlen(needle);
  if (needleLen == 0 || size < needleLen) return size;
  for (size_t i = size - needleLen + 1; i-- > 0;) {
    if (std::memcmp(data + i, needle, needleLen) == 0) return i;
  }
  return size;
}

bool inflateBuffer(const uint8_t* src, size_t srcLen, std::vector<uint8_t>& out) {
  if (srcLen < 2) return false;
  InflateReader reader;
  // Streaming (not one-shot) mode: readAtMost() is called repeatedly below into a small reused chunk buffer,
  // and one-shot mode resets its back-reference base to that chunk on every call (see InflateReader.cpp),
  // which corrupts any back-reference reaching outside the current chunk. Streaming mode's ring buffer is
  // what makes repeated readAtMost() calls valid - the input itself is still supplied in one go via setSource().
  if (!reader.init(true)) return false;
  reader.setSource(src, srcLen);
  reader.skipZlibHeader();

  out.clear();
  uint8_t chunk[2048];
  int chunksSinceYield = 0;
  int consecutiveZeroProgress = 0;
  while (true) {
    size_t produced = 0;
    const InflateStatus status = reader.readAtMost(chunk, sizeof(chunk), &produced);
    if (produced > 0) {
      out.insert(out.end(), chunk, chunk + produced);
      consecutiveZeroProgress = 0;
    } else if (++consecutiveZeroProgress > 64) {
      // Status::Ok with zero bytes produced, repeatedly: truncated/malformed compressed data can make the
      // decompressor report "keep going" forever without actually progressing. Fail instead of hanging.
      return false;
    }
    if (status == InflateStatus::Done) return true;
    if (status == InflateStatus::Error) return false;
    if (out.size() > 48u * 1024u * 1024u) return false;  // runaway safety valve
    if (++chunksSinceYield >= 32) {  // feed the watchdog on large streams (~64KB decompressed per yield)
      chunksSinceYield = 0;
      vTaskDelay(1);
    }
  }
}

uint64_t readBigEndian(const uint8_t* data, const size_t width) {
  uint64_t value = 0;
  for (size_t i = 0; i < width; i++) value = (value << 8) | data[i];
  return value;
}

bool decodeAsciiHex(const uint8_t* src, size_t srcLen, std::vector<uint8_t>& out) {
  out.clear();
  int hi = -1;
  for (size_t i = 0; i < srcLen; i++) {
    const uint8_t c = src[i];
    if (c == '>') break;
    int v;
    if (c >= '0' && c <= '9') v = c - '0';
    else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
    else continue;
    if (hi < 0) {
      hi = v;
    } else {
      out.push_back(static_cast<uint8_t>((hi << 4) | v));
      hi = -1;
    }
  }
  if (hi >= 0) out.push_back(static_cast<uint8_t>(hi << 4));
  return true;
}

bool decodeAscii85(const uint8_t* src, size_t srcLen, std::vector<uint8_t>& out) {
  out.clear();
  size_t i = 0;
  if (srcLen >= 2 && src[0] == '<' && src[1] == '~') i = 2;  // optional leading delimiter

  uint32_t group[5];
  int groupLen = 0;
  for (; i < srcLen; i++) {
    const uint8_t c = src[i];
    if (c == '~') break;
    if (c == 'z' && groupLen == 0) {
      out.insert(out.end(), 4, 0);
      continue;
    }
    if (c < '!' || c > 'u') continue;  // whitespace or invalid - skip
    group[groupLen++] = c - '!';
    if (groupLen == 5) {
      uint32_t value = 0;
      for (int k = 0; k < 5; k++) value = value * 85 + group[k];
      out.push_back(static_cast<uint8_t>(value >> 24));
      out.push_back(static_cast<uint8_t>(value >> 16));
      out.push_back(static_cast<uint8_t>(value >> 8));
      out.push_back(static_cast<uint8_t>(value));
      groupLen = 0;
    }
  }
  if (groupLen > 0) {
    const int missing = 5 - groupLen;
    for (int k = groupLen; k < 5; k++) group[k] = 84;  // pad with 'u' (84)
    uint32_t value = 0;
    for (int k = 0; k < 5; k++) value = value * 85 + group[k];
    uint8_t bytes[4] = {static_cast<uint8_t>(value >> 24), static_cast<uint8_t>(value >> 16),
                        static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value)};
    out.insert(out.end(), bytes, bytes + (4 - missing));
  }
  return true;
}

}  // namespace

bool PdfDocument::open(const std::string& path) {
  close();

  FsFile file;
  if (!SdMan.openFileForRead("PDF", path, file)) {
    lastError_ = OpenError::FileNotFound;
    return false;
  }

  const uint64_t size = file.size();
  if (size > kMaxSupportedFileSize) {
    file.close();
    lastError_ = OpenError::TooLarge;
    return false;
  }

  fileData_.resize(static_cast<size_t>(size));
  size_t totalRead = 0;
  while (totalRead < fileData_.size()) {
    const size_t chunk = file.read(fileData_.data() + totalRead, fileData_.size() - totalRead);
    if (chunk == 0) break;
    totalRead += chunk;
  }
  file.close();

  if (totalRead != fileData_.size() || fileData_.size() < 8 ||
      std::memcmp(fileData_.data(), "%PDF-", 5) != 0) {
    lastError_ = OpenError::NotAPdf;
    fileData_.clear();
    return false;
  }

  if (!parseXrefChain()) {
    fileData_.clear();
    return false;
  }

  const PdfObject* rootRef = trailer_.find("Root");
  if (!rootRef) {
    lastError_ = OpenError::NoRootCatalog;
    fileData_.clear();
    return false;
  }
  const PdfObject root = resolve(*rootRef);
  const PdfObject* pagesRef = root.find("Pages");
  if (!pagesRef) {
    lastError_ = OpenError::NoRootCatalog;
    fileData_.clear();
    return false;
  }

  std::vector<uint32_t> visitedNodes;
  PdfObject emptyInherited;
  emptyInherited.type = PdfObject::Type::Dictionary;
  collectPages(*pagesRef, emptyInherited, visitedNodes, 0);

  if (pages_.empty()) {
    lastError_ = OpenError::NoPages;
    fileData_.clear();
    return false;
  }

  loaded_ = true;
  lastError_ = OpenError::None;
  return true;
}

void PdfDocument::close() {
  fileData_.clear();
  fileData_.shrink_to_fit();
  loaded_ = false;
  trailer_ = PdfObject();
  xrefEntries_.clear();
  objectCache_.clear();
  pages_.clear();
}

bool PdfDocument::parseXrefChain() {
  const size_t startxrefPos = findLastOccurrence(fileData_.data(), fileData_.size(), "startxref");
  if (startxrefPos == fileData_.size()) {
    lastError_ = OpenError::NoStartxref;
    return false;
  }

  PdfLexer offsetLexer(fileData_.data(), fileData_.size(), startxrefPos + std::strlen("startxref"));
  const PdfToken offsetTok = offsetLexer.next();
  if (offsetTok.type != PdfTokenType::Number) {
    lastError_ = OpenError::NoStartxref;
    return false;
  }

  std::vector<uint64_t> visited;
  uint64_t offset = static_cast<uint64_t>(offsetTok.number);
  bool first = true;

  while (offset < fileData_.size()) {
    if (std::find(visited.begin(), visited.end(), offset) != visited.end()) break;
    visited.push_back(offset);

    PdfObject sectionTrailer;
    if (!parseXrefSectionAt(offset, visited, sectionTrailer)) {
      if (first) return false;  // first/only section unusable -> whole document unsupported
      break;                    // a /Prev link is broken - keep what we already parsed
    }

    if (trailer_.isNull()) {
      trailer_ = sectionTrailer;
    } else {
      for (const auto& kv : sectionTrailer.dictValue) {
        if (trailer_.dictValue.find(kv.first) == trailer_.dictValue.end()) {
          trailer_.dictValue[kv.first] = kv.second;
        }
      }
    }

    const PdfObject* prev = sectionTrailer.find("Prev");
    if (!prev || !prev->isNumber()) break;
    offset = static_cast<uint64_t>(prev->asNumber());
    first = false;
  }

  if (trailer_.isNull()) {
    lastError_ = OpenError::NoTrailer;
    return false;
  }
  return true;
}

bool PdfDocument::parseXrefSectionAt(const uint64_t offset, std::vector<uint64_t>& visitedOffsets,
                                     PdfObject& outSectionTrailer) {
  (void)visitedOffsets;
  PdfLexer lexer(fileData_.data(), fileData_.size(), static_cast<size_t>(offset));
  const PdfToken first = lexer.next();

  if (first.type != PdfTokenType::Keyword || first.text != "xref") {
    // PDF 1.5+ commonly stores the cross-reference table in a compressed stream. The stream's indirect object
    // starts exactly at the startxref offset, so decode it through the same stream path as page content.
    return parseXrefStreamAt(offset, outSectionTrailer);
  }

  while (true) {
    const PdfToken sectionTok = lexer.peek();
    if (sectionTok.type == PdfTokenType::Keyword && sectionTok.text == "trailer") {
      lexer.next();
      break;
    }
    if (sectionTok.type != PdfTokenType::Number) break;  // malformed - stop, try to parse trailer anyway

    lexer.next();
    const uint32_t startNum = static_cast<uint32_t>(sectionTok.number);
    const PdfToken countTok = lexer.next();
    if (countTok.type != PdfTokenType::Number) break;
    const uint32_t count = static_cast<uint32_t>(countTok.number);

    for (uint32_t i = 0; i < count; i++) {
      const PdfToken offTok = lexer.next();
      const PdfToken genTok = lexer.next();
      const PdfToken typeTok = lexer.next();
      (void)genTok;
      if (offTok.type != PdfTokenType::Number || typeTok.type != PdfTokenType::Keyword) continue;
      if (typeTok.text == "n") {
        const uint32_t objNum = startNum + i;
        if (xrefEntries_.find(objNum) == xrefEntries_.end()) {
          xrefEntries_[objNum] = XrefEntry{1, static_cast<uint64_t>(offTok.number), 0, 0};
        }
      }
    }
  }

  outSectionTrailer = parseNextPdfValue(lexer);
  return outSectionTrailer.isDict();
}

bool PdfDocument::parseXrefStreamAt(const uint64_t offset, PdfObject& outSectionTrailer) {
  const PdfObject stream = parseIndirectObjectAt(offset);
  if (!stream.isStream()) {
    lastError_ = OpenError::UnsupportedXrefStream;
    return false;
  }

  const PdfObject* type = stream.find("Type");
  const PdfObject* widthObj = stream.find("W");
  const PdfObject* sizeObj = stream.find("Size");
  if (!type || !type->isName() || type->strValue != "XRef" || !widthObj || !sizeObj) {
    lastError_ = OpenError::UnsupportedXrefStream;
    return false;
  }

  const PdfObject widths = resolve(*widthObj);
  const int size = resolve(*sizeObj).asInt(0);
  if (!widths.isArray() || widths.arrValue.size() < 3 || size <= 0) {
    lastError_ = OpenError::NoTrailer;
    return false;
  }
  const size_t w0 = static_cast<size_t>(resolve(widths.arrValue[0]).asInt(0));
  const size_t w1 = static_cast<size_t>(resolve(widths.arrValue[1]).asInt(0));
  const size_t w2 = static_cast<size_t>(resolve(widths.arrValue[2]).asInt(0));
  const size_t recordWidth = w0 + w1 + w2;
  if (w0 > 8 || w1 > 8 || w2 > 8 || recordWidth == 0) {
    lastError_ = OpenError::UnsupportedXrefStream;
    return false;
  }

  std::vector<uint8_t> bytes;
  if (!getStreamBytes(stream, bytes)) {
    lastError_ = OpenError::UnsupportedXrefStream;
    return false;
  }

  std::vector<std::pair<uint32_t, uint32_t>> ranges;
  if (const PdfObject* indexObj = stream.find("Index")) {
    const PdfObject index = resolve(*indexObj);
    if (!index.isArray() || index.arrValue.size() % 2 != 0) {
      lastError_ = OpenError::NoTrailer;
      return false;
    }
    for (size_t i = 0; i < index.arrValue.size(); i += 2) {
      ranges.emplace_back(static_cast<uint32_t>(resolve(index.arrValue[i]).asInt(0)),
                          static_cast<uint32_t>(resolve(index.arrValue[i + 1]).asInt(0)));
    }
  } else {
    ranges.emplace_back(0, static_cast<uint32_t>(size));
  }

  size_t cursor = 0;
  for (const auto& range : ranges) {
    for (uint32_t i = 0; i < range.second; i++) {
      if (cursor + recordWidth > bytes.size()) {
        lastError_ = OpenError::NoTrailer;
        return false;
      }
      const uint64_t field0 = w0 == 0 ? 1 : readBigEndian(bytes.data() + cursor, w0);
      cursor += w0;
      const uint64_t field1 = w1 == 0 ? 0 : readBigEndian(bytes.data() + cursor, w1);
      cursor += w1;
      const uint64_t field2 = w2 == 0 ? 0 : readBigEndian(bytes.data() + cursor, w2);
      cursor += w2;

      const uint32_t objectNumber = range.first + i;
      if (xrefEntries_.find(objectNumber) != xrefEntries_.end()) continue;
      if (field0 == 1) {
        xrefEntries_[objectNumber] = XrefEntry{1, field1, 0, 0};
      } else if (field0 == 2) {
        xrefEntries_[objectNumber] = XrefEntry{2, 0, static_cast<uint32_t>(field1),
                                                static_cast<uint32_t>(field2)};
      }
    }
  }

  outSectionTrailer = stream;
  return true;
}

PdfObject PdfDocument::parseIndirectObjectAt(const uint64_t offset) const {
  PdfLexer lexer(fileData_.data(), fileData_.size(), static_cast<size_t>(offset));

  const PdfToken numTok = lexer.next();
  const PdfToken genTok = lexer.next();
  const PdfToken objTok = lexer.next();
  (void)genTok;
  if (numTok.type != PdfTokenType::Number || objTok.type != PdfTokenType::Keyword || objTok.text != "obj") {
    return PdfObject::makeNull();
  }

  PdfObject value = parseNextPdfValue(lexer);

  const PdfToken maybeStream = lexer.peek();
  if (maybeStream.type == PdfTokenType::Keyword && maybeStream.text == "stream" && value.isDict()) {
    lexer.next();
    size_t dataStart = lexer.position();
    // "stream" is followed by CRLF or LF (spec-mandated), tolerate a lone CR too.
    if (dataStart < fileData_.size() && fileData_[dataStart] == '\r') dataStart++;
    if (dataStart < fileData_.size() && fileData_[dataStart] == '\n') dataStart++;

    const PdfObject* lengthObj = value.find("Length");
    size_t length = 0;
    bool lengthResolved = false;
    if (lengthObj) {
      const PdfObject resolvedLength = lengthObj->isReference() ? resolve(*lengthObj) : *lengthObj;
      if (resolvedLength.isNumber() && resolvedLength.asInt(0) >= 0) {
        length = static_cast<size_t>(resolvedLength.asInt(0));
        lengthResolved = true;
      }
    }
    if (!lengthResolved) {
      // Cross-reference streams can refer to an indirect /Length before the xref stream has populated the
      // object table. Recover the stream extent from the required endstream marker in that case.
      const size_t remaining = fileData_.size() - dataStart;
      size_t endstream = remaining;
      for (size_t i = 0; i + 9 <= remaining; i++) {
        if (std::memcmp(fileData_.data() + dataStart + i, "endstream", 9) == 0) {
          endstream = i;
          break;
        }
      }
      if (endstream != remaining) {
        length = endstream;
      }
    }
    if (dataStart + length > fileData_.size()) {
      length = fileData_.size() > dataStart ? fileData_.size() - dataStart : 0;
    }

    value.type = PdfObject::Type::Stream;
    value.streamOffset = dataStart;
    value.streamLength = length;
  }

  return value;
}

PdfObject PdfDocument::resolve(const PdfObject& obj) const {
  if (!obj.isReference()) return obj;
  return getObject(obj.refNum);
}

const PdfObject& PdfDocument::getObject(const uint32_t num) const {
  const auto cached = objectCache_.find(num);
  if (cached != objectCache_.end()) return cached->second;

  const auto entryIt = xrefEntries_.find(num);
  if (entryIt == xrefEntries_.end() || entryIt->second.type == 0) {
    return objectCache_.emplace(num, PdfObject::makeNull()).first->second;
  }

  PdfObject parsed = entryIt->second.type == 2 ? parseCompressedObject(entryIt->second)
                                               : parseIndirectObjectAt(entryIt->second.offset);
  return objectCache_.emplace(num, std::move(parsed)).first->second;
}

PdfObject PdfDocument::parseCompressedObject(const XrefEntry& entry) const {
  const PdfObject& objectStream = getObject(entry.objectStream);
  if (!objectStream.isStream()) return PdfObject::makeNull();

  const PdfObject* firstObj = objectStream.find("First");
  const PdfObject* countObj = objectStream.find("N");
  if (!firstObj || !countObj) return PdfObject::makeNull();
  const size_t first = static_cast<size_t>(resolve(*firstObj).asInt(0));
  const int count = resolve(*countObj).asInt(0);
  if (count <= 0) return PdfObject::makeNull();

  std::vector<uint8_t> bytes;
  if (!getStreamBytes(objectStream, bytes) || first >= bytes.size()) return PdfObject::makeNull();

  PdfLexer headerLexer(bytes.data(), first, 0);
  std::vector<std::pair<uint32_t, uint32_t>> header;
  header.reserve(static_cast<size_t>(count));
  for (int i = 0; i < count; i++) {
    const PdfToken number = headerLexer.next();
    const PdfToken relative = headerLexer.next();
    if (number.type != PdfTokenType::Number || relative.type != PdfTokenType::Number) return PdfObject::makeNull();
    header.emplace_back(static_cast<uint32_t>(number.number), static_cast<uint32_t>(relative.number));
  }
  if (entry.objectStreamIndex >= header.size()) return PdfObject::makeNull();

  const size_t objectStart = first + header[entry.objectStreamIndex].second;
  if (objectStart >= bytes.size()) return PdfObject::makeNull();
  PdfLexer objectLexer(bytes.data(), bytes.size(), objectStart);
  return parseNextPdfValue(objectLexer);
}

bool PdfDocument::getStreamBytes(const PdfObject& streamObj, std::vector<uint8_t>& out) const {
  if (!streamObj.isStream()) return false;
  if (streamObj.streamOffset + streamObj.streamLength > fileData_.size()) return false;

  std::vector<std::string> filters;
  if (const PdfObject* filterObj = streamObj.find("Filter")) {
    const PdfObject resolved = resolve(*filterObj);
    if (resolved.isName()) {
      filters.push_back(resolved.strValue);
    } else if (resolved.isArray()) {
      for (const auto& f : resolved.arrValue) {
        const PdfObject rf = resolve(f);
        if (rf.isName()) filters.push_back(rf.strValue);
      }
    }
  }

  std::vector<uint8_t> current(fileData_.begin() + static_cast<long>(streamObj.streamOffset),
                               fileData_.begin() + static_cast<long>(streamObj.streamOffset + streamObj.streamLength));

  for (const auto& filter : filters) {
    std::vector<uint8_t> next;
    bool ok;
    if (filter == "FlateDecode" || filter == "Fl") {
      ok = inflateBuffer(current.data(), current.size(), next);
    } else if (filter == "ASCIIHexDecode" || filter == "AHx") {
      ok = decodeAsciiHex(current.data(), current.size(), next);
    } else if (filter == "ASCII85Decode" || filter == "A85") {
      ok = decodeAscii85(current.data(), current.size(), next);
    } else {
      INX_SERIAL.printf("[%lu] [PDF] Unsupported stream filter: %s\n", millis(), filter.c_str());
      return false;
    }
    if (!ok) {
      INX_SERIAL.printf("[%lu] [PDF] Filter %s failed on a %zu-byte stream\n", millis(), filter.c_str(),
                    current.size());
      return false;
    }
    current = std::move(next);
  }

  out = std::move(current);
  return true;
}

bool PdfDocument::getRawStreamBytes(const PdfObject& streamObj, std::vector<uint8_t>& out) const {
  if (!streamObj.isStream() || streamObj.streamOffset + streamObj.streamLength > fileData_.size()) return false;
  out.assign(fileData_.begin() + static_cast<long>(streamObj.streamOffset),
             fileData_.begin() + static_cast<long>(streamObj.streamOffset + streamObj.streamLength));
  return true;
}

void PdfDocument::collectPages(const PdfObject& nodeRef, const PdfObject& inheritedAttrs,
                               std::vector<uint32_t>& visitedNodes, const int depth) {
  if (depth > 64) return;
  if (nodeRef.isReference()) {
    if (std::find(visitedNodes.begin(), visitedNodes.end(), nodeRef.refNum) != visitedNodes.end()) return;
    visitedNodes.push_back(nodeRef.refNum);
  }

  const PdfObject node = resolve(nodeRef);
  if (!node.isDict()) return;

  PdfObject combined = inheritedAttrs;
  for (const char* key : {"Resources", "MediaBox", "Rotate"}) {
    if (const PdfObject* v = node.find(key)) {
      combined.dictValue[key] = *v;
    }
  }

  const PdfObject* kids = node.find("Kids");
  if (kids) {
    const PdfObject resolvedKids = resolve(*kids);
    if (resolvedKids.isArray()) {
      for (const auto& kid : resolvedKids.arrValue) {
        collectPages(kid, combined, visitedNodes, depth + 1);
      }
      return;
    }
  }

  // Leaf page (Type /Page, or no /Kids array present).
  PageEntry entry;
  entry.ref = nodeRef;
  entry.inheritedAttrs = combined;
  pages_.push_back(std::move(entry));
}

bool PdfDocument::getPage(const int index, PdfObject& outPageDict) const {
  if (index < 0 || index >= static_cast<int>(pages_.size())) return false;

  const PageEntry& entry = pages_[static_cast<size_t>(index)];
  outPageDict = resolve(entry.ref);
  if (!outPageDict.isDict()) return false;

  for (const char* key : {"Resources", "MediaBox", "Rotate"}) {
    if (outPageDict.find(key) == nullptr) {
      if (const PdfObject* inherited = entry.inheritedAttrs.find(key)) {
        outPageDict.dictValue[key] = *inherited;
      }
    }
  }
  return true;
}

std::string PdfDocument::getTitleFromInfo() const {
  const PdfObject* infoRef = trailer_.find("Info");
  if (!infoRef) return "";
  const PdfObject info = resolve(*infoRef);
  const PdfObject* title = info.find("Title");
  if (!title) return "";
  const PdfObject resolvedTitle = resolve(*title);
  return resolvedTitle.isString() ? resolvedTitle.strValue : "";
}
