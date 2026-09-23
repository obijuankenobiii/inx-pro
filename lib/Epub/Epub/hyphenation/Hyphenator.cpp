/**
 * @file Hyphenator.cpp
 * @brief Definitions for Hyphenator.
 */

#include "Hyphenator.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "HyphenationCommon.h"
#include "FibhHyphenation.h"
#include "LiangHyphenation.h"
#include "LanguageRegistry.h"

#if defined(ARDUINO_ARCH_ESP32)
#include <Arduino.h>
#include <HardwareSerial.h>
#include <SDCardManager.h>
#include <esp_heap_caps.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "../../../../src/util/SdIoMutex.h"
#endif

const LanguageHyphenator* Hyphenator::cachedHyphenator_ = nullptr;

namespace {
uint8_t* customHyphenationData = nullptr;
size_t customHyphenationSize = 0;
bool customHyphenationIsFibh = false;
std::string customHyphenationPath;
std::string customHyphenationLanguage;
uint16_t customHyphenationModifiedDate = 0;
uint16_t customHyphenationModifiedTime = 0;
uint32_t customHyphenationHash = 0;

const LanguageHyphenator* hyphenatorForLanguage(const std::string& langTag) {
  if (langTag.empty()) return nullptr;

  std::string primary;
  primary.reserve(langTag.size());
  for (char c : langTag) {
    if (c == '-' || c == '_') break;
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    primary.push_back(c);
  }
  if (primary.empty()) return nullptr;

  return getLanguageHyphenatorForPrimaryTag(primary);
}

std::string primaryLanguage(const std::string& langTag) {
  std::string primary;
  primary.reserve(langTag.size());
  for (char c : langTag) {
    if (c == '-' || c == '_') break;
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    primary.push_back(c);
  }
  return primary;
}

void freeCustomHyphenation() {
#if defined(ARDUINO_ARCH_ESP32)
  heap_caps_free(customHyphenationData);
#else
  free(customHyphenationData);
#endif
  customHyphenationData = nullptr;
  customHyphenationSize = 0;
  customHyphenationIsFibh = false;
  customHyphenationPath.clear();
  customHyphenationLanguage.clear();
  customHyphenationModifiedDate = 0;
  customHyphenationModifiedTime = 0;
  customHyphenationHash = 0;
}

#if defined(ARDUINO_ARCH_ESP32)
uint8_t* allocateCustomHyphenation(const size_t size) {
  if (auto* psram = static_cast<uint8_t*>(heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT))) {
    return psram;
  }
  return static_cast<uint8_t*>(heap_caps_malloc(size, MALLOC_CAP_8BIT));
}

bool safeLanguagePathPart(const std::string& value) {
  if (value.empty() || value.size() > 32) return false;
  for (const unsigned char c : value) {
    if (!std::isalnum(c) && c != '-' && c != '_') return false;
  }
  return true;
}

bool loadCustomHyphenation(const std::string& langTag) {
  std::vector<std::string> languageCodes;
  const auto addCode = [&languageCodes](const std::string& code) {
    if (!code.empty() && std::find(languageCodes.begin(), languageCodes.end(), code) == languageCodes.end()) {
      languageCodes.push_back(code);
    }
  };
  addCode(langTag);
  std::string lowercase = langTag;
  std::transform(lowercase.begin(), lowercase.end(), lowercase.begin(),
                 [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
  addCode(lowercase);
  addCode(primaryLanguage(langTag));

  SdIoMutex::Lock ioLock;
  for (const std::string& code : languageCodes) {
    if (!safeLanguagePathPart(code)) continue;
    const std::string path = "/.system/lang/" + code + "/hyphenation.bin";
    FsFile file = SdMan.open(path.c_str(), O_READ);
    if (!file) continue;

    const size_t fileSize = file.size();
    uint16_t modifiedDate = 0;
    uint16_t modifiedTime = 0;
    file.getModifyDateTime(&modifiedDate, &modifiedTime);
    if (path == customHyphenationPath && fileSize == customHyphenationSize &&
        modifiedDate == customHyphenationModifiedDate && modifiedTime == customHyphenationModifiedTime &&
        customHyphenationData) {
      file.close();
      customHyphenationLanguage = langTag;
      return true;
    }

    freeCustomHyphenation();
    constexpr size_t kMaxCustomTrieBytes = 5 * 1024 * 1024;
    if (fileSize < 8 || fileSize > kMaxCustomTrieBytes) {
      INX_SERIAL.printf("[%lu] [HYPH] ignored invalid custom trie path=%s bytes=%lu\n", millis(), path.c_str(),
                        static_cast<unsigned long>(fileSize));
      file.close();
      continue;
    }

    uint8_t* data = allocateCustomHyphenation(fileSize);
    uint8_t* ioBuffer = static_cast<uint8_t*>(heap_caps_malloc(2048, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (!ioBuffer) ioBuffer = static_cast<uint8_t*>(heap_caps_malloc(2048, MALLOC_CAP_8BIT));
    if (!data || !ioBuffer) {
      heap_caps_free(data);
      heap_caps_free(ioBuffer);
      file.close();
      continue;
    }

    size_t loaded = 0;
    while (loaded < fileSize) {
      const size_t request = std::min<size_t>(2048, fileSize - loaded);
      const size_t got = file.read(ioBuffer, request);
      if (got == 0) break;
      memcpy(data + loaded, ioBuffer, got);
      loaded += got;
      if ((loaded & 0xFFFFu) == 0) {
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(1));
      }
    }
    file.close();
    heap_caps_free(ioBuffer);
    if (loaded != fileSize) {
      heap_caps_free(data);
      continue;
    }

    bool isFibh = isValidFibhHyphenation(data, fileSize);
    if (!isFibh) {
      const uint32_t root = (static_cast<uint32_t>(data[0]) << 24) | (static_cast<uint32_t>(data[1]) << 16) |
                            (static_cast<uint32_t>(data[2]) << 8) | data[3];
      if (root >= fileSize) {
        INX_SERIAL.printf("[%lu] [HYPH] ignored unsupported custom trie path=%s\n", millis(), path.c_str());
        heap_caps_free(data);
        continue;
      }
    }

    customHyphenationData = data;
    customHyphenationSize = fileSize;
    customHyphenationIsFibh = isFibh;
    customHyphenationPath = path;
    customHyphenationLanguage = langTag;
    customHyphenationModifiedDate = modifiedDate;
    customHyphenationModifiedTime = modifiedTime;
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < fileSize; ++i) hash = (hash ^ data[i]) * 16777619u;
    customHyphenationHash = hash ? hash : 1;
    INX_SERIAL.printf("[%lu] [HYPH] custom patterns loaded lang=%s format=%s bytes=%lu\n", millis(), langTag.c_str(),
                      isFibh ? "FIBH" : "Hypher", static_cast<unsigned long>(fileSize));
    return true;
  }

  freeCustomHyphenation();
  return false;
}
#endif

size_t byteOffsetForIndex(const std::vector<CodepointInfo>& cps, const size_t index) {
  return (index < cps.size()) ? cps[index].byteOffset : (cps.empty() ? 0 : cps.back().byteOffset);
}

std::vector<Hyphenator::BreakInfo> buildExplicitBreakInfos(const std::vector<CodepointInfo>& cps) {
  std::vector<Hyphenator::BreakInfo> breaks;

  for (size_t i = 1; i + 1 < cps.size(); ++i) {
    const uint32_t cp = cps[i].value;
    if (!isExplicitHyphen(cp) || !isAlphabetic(cps[i - 1].value) || !isAlphabetic(cps[i + 1].value)) {
      continue;
    }

    breaks.push_back({cps[i + 1].byteOffset, isSoftHyphen(cp)});
  }

  return breaks;
}

}

std::vector<Hyphenator::BreakInfo> Hyphenator::breakOffsets(const std::string& word, const bool includeFallback) {
  if (word.empty()) {
    return {};
  }

  auto cps = collectCodepoints(word);
  trimSurroundingPunctuationAndFootnote(cps);
  const auto* hyphenator = cachedHyphenator_;

  auto explicitBreakInfos = buildExplicitBreakInfos(cps);
  if (!explicitBreakInfos.empty()) {
    return explicitBreakInfos;
  }

  std::vector<size_t> indexes;
  if (customHyphenationData && customHyphenationIsFibh) {
    indexes = fibhBreakIndexes(word, customHyphenationData, customHyphenationSize);
  } else if (customHyphenationData) {
    const SerializedHyphenationPatterns patterns{customHyphenationData, customHyphenationSize};
    const std::string primary = primaryLanguage(customHyphenationLanguage);
    const LiangWordConfig config = (primary == "ru")
                                       ? LiangWordConfig(isCyrillicLetter, toLowerCyrillic)
                                       : LiangWordConfig(isLatinLetter, toLowerLatin);
    indexes = liangBreakIndexes(cps, patterns, config);
  } else if (hyphenator) {
    indexes = hyphenator->breakIndexes(cps);
  }

  if (includeFallback && indexes.empty()) {
    const size_t minPrefix = hyphenator ? hyphenator->minPrefix() : LiangWordConfig::kDefaultMinPrefix;
    const size_t minSuffix = hyphenator ? hyphenator->minSuffix() : LiangWordConfig::kDefaultMinSuffix;
    for (size_t idx = minPrefix; idx + minSuffix <= cps.size(); ++idx) {
      indexes.push_back(idx);
    }
  }

  if (indexes.empty()) {
    return {};
  }

  std::vector<Hyphenator::BreakInfo> breaks;
  breaks.reserve(indexes.size());
  for (const size_t idx : indexes) {
    breaks.push_back({byteOffsetForIndex(cps, idx), true});
  }

  return breaks;
}

void Hyphenator::setPreferredLanguage(const std::string& lang) {
  cachedHyphenator_ = hyphenatorForLanguage(lang);
#if defined(ARDUINO_ARCH_ESP32)
  loadCustomHyphenation(lang);
#else
  if (customHyphenationLanguage != lang) freeCustomHyphenation();
#endif
}

uint32_t Hyphenator::cacheSignature() { return customHyphenationHash; }
