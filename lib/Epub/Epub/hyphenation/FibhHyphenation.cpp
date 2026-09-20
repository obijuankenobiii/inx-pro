#include "FibhHyphenation.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace {

uint32_t readLe32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

uint32_t foldCase(uint32_t cp) {
  if (cp >= 'A' && cp <= 'Z') return cp + 0x20;
  if (cp >= 0x00C0 && cp <= 0x00DE && cp != 0x00D7) return cp + 0x20;
  if (cp >= 0x0100 && cp <= 0x0137 && (cp & 1) == 0) return cp + 1;
  if (cp >= 0x0139 && cp <= 0x0148 && (cp & 1) == 1) return cp + 1;
  if (cp >= 0x014A && cp <= 0x0177 && (cp & 1) == 0) return cp + 1;
  if (cp == 0x0178) return 0x00FF;
  if (cp >= 0x0179 && cp <= 0x017E && (cp & 1) == 1) return cp + 1;
  if (cp >= 0x0391 && cp <= 0x03A9 && cp != 0x03A2) return cp + 0x20;
  if (cp >= 0x0410 && cp <= 0x042F) return cp + 0x20;
  if (cp >= 0x0400 && cp <= 0x040F) return cp + 0x50;
  return cp;
}

void appendUtf8(uint32_t cp, std::vector<uint8_t>& out) {
  if (cp < 0x80) {
    out.push_back(static_cast<uint8_t>(cp));
  } else if (cp < 0x800) {
    out.push_back(static_cast<uint8_t>(0xC0 | (cp >> 6)));
    out.push_back(static_cast<uint8_t>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    out.push_back(static_cast<uint8_t>(0xE0 | (cp >> 12)));
    out.push_back(static_cast<uint8_t>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<uint8_t>(0x80 | (cp & 0x3F)));
  } else {
    out.push_back(static_cast<uint8_t>(0xF0 | (cp >> 18)));
    out.push_back(static_cast<uint8_t>(0x80 | ((cp >> 12) & 0x3F)));
    out.push_back(static_cast<uint8_t>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<uint8_t>(0x80 | (cp & 0x3F)));
  }
}

struct Table {
  const uint8_t* offsets = nullptr;
  const uint8_t* records = nullptr;
  uint32_t count = 0;
  uint8_t maxKeyLength = 0;
};

bool parseTable(const uint8_t* data, size_t size, Table& table, const bool validateRecords) {
  if (!data || size < 12 || memcmp(data, "FIBH", 4) != 0 ||
      (static_cast<uint16_t>(data[4]) | (static_cast<uint16_t>(data[5]) << 8)) != 1) return false;
  const uint32_t count = readLe32(data + 8);
  if (count == 0 || count > (size - 12) / 4) return false;
  const size_t recordsOffset = 12 + static_cast<size_t>(count) * 4;
  const uint8_t* offsets = data + 12;
  const uint8_t* records = data + recordsOffset;
  const size_t recordsSize = size - recordsOffset;
  const uint8_t maxKeyLength = data[6];
  if (maxKeyLength == 0) return false;

  if (validateRecords) {
    for (uint32_t i = 0; i < count; ++i) {
      const uint32_t offset = readLe32(offsets + static_cast<size_t>(i) * 4);
      if (offset >= recordsSize) return false;
      const uint8_t keyLength = records[offset];
      const size_t recordSize = 1 + static_cast<size_t>(keyLength) * 2 + 1;
      if (keyLength == 0 || keyLength > maxKeyLength || recordSize > recordsSize - offset) return false;
    }
  }

  table = {offsets, records, count, maxKeyLength};
  return true;
}

const uint8_t* keyAt(const Table& table, uint32_t index, uint8_t& keyLength) {
  const uint8_t* record = table.records + readLe32(table.offsets + static_cast<size_t>(index) * 4);
  keyLength = record[0];
  return record + 1;
}

}  // namespace

bool isValidFibhHyphenation(const uint8_t* data, const size_t size) {
  Table table;
  return parseTable(data, size, table, true);
}

std::vector<size_t> fibhBreakIndexes(const std::string& word, const uint8_t* data, const size_t size,
                                     const size_t minPrefix, const size_t minSuffix) {
  Table table;
  if (word.empty() || !parseTable(data, size, table, false)) return {};

  std::vector<uint8_t> marked;
  std::vector<size_t> byteToChar;
  marked.reserve(word.size() + 2);
  byteToChar.resize(word.size(), 0);
  marked.push_back('.');

  const auto* source = reinterpret_cast<const uint8_t*>(word.data());
  size_t i = 0;
  size_t charCount = 0;
  while (i < word.size()) {
    const size_t start = i;
    const uint8_t first = source[i++];
    uint32_t cp = first;
    size_t continuation = 0;
    if ((first & 0xE0) == 0xC0) { cp = first & 0x1F; continuation = 1; }
    else if ((first & 0xF0) == 0xE0) { cp = first & 0x0F; continuation = 2; }
    else if ((first & 0xF8) == 0xF0) { cp = first & 0x07; continuation = 3; }
    if (i + continuation > word.size()) return {};
    for (size_t j = 0; j < continuation; ++j) {
      if ((source[i] & 0xC0) != 0x80) return {};
      cp = (cp << 6) | (source[i++] & 0x3F);
    }

    std::vector<uint8_t> encoded;
    appendUtf8(foldCase(cp), encoded);
    if (encoded.size() != i - start) return {};
    for (size_t j = start; j < i; ++j) byteToChar[j] = static_cast<uint16_t>(charCount);
    for (const uint8_t byte : encoded) marked.push_back(byte);
    ++charCount;
  }
  marked.push_back('.');
  if (charCount < minPrefix + minSuffix) return {};

  std::vector<uint8_t> values(marked.size() + 1, 0);
  for (size_t start = 0; start < marked.size(); ++start) {
    uint32_t lo = 0;
    uint32_t hi = table.count;
    const size_t maxLength = std::min<size_t>(table.maxKeyLength, marked.size() - start);
    for (size_t depth = 0; depth < maxLength && lo < hi; ++depth) {
      const uint8_t c = marked[start + depth];
      uint32_t a = lo;
      uint32_t b = hi;
      while (a < b) {
        const uint32_t mid = a + (b - a) / 2;
        uint8_t keyLength = 0;
        const uint8_t* key = keyAt(table, mid, keyLength);
        if (keyLength <= depth || key[depth] < c) a = mid + 1;
        else b = mid;
      }
      lo = a;
      b = hi;
      while (a < b) {
        const uint32_t mid = a + (b - a) / 2;
        uint8_t keyLength = 0;
        const uint8_t* key = keyAt(table, mid, keyLength);
        if (keyLength <= depth || key[depth] <= c) a = mid + 1;
        else b = mid;
      }
      hi = a;
      if (lo >= hi) break;

      uint8_t keyLength = 0;
      const uint8_t* key = keyAt(table, lo, keyLength);
      if (keyLength == depth + 1) {
        const uint8_t* scores = key + keyLength;
        for (size_t v = 0; v <= keyLength; ++v) {
          values[start + v] = std::max(values[start + v], scores[v]);
        }
      }
    }
  }

  std::vector<size_t> breaks;
  for (size_t scoreIndex = 1; scoreIndex + 1 < marked.size(); ++scoreIndex) {
    if ((values[scoreIndex] & 1u) == 0) continue;
    const size_t byteOffset = scoreIndex - 1;
    if (byteOffset >= word.size() || (source[byteOffset] & 0xC0) == 0x80) continue;
    const size_t leftChars = byteToChar[byteOffset];
    if (leftChars < minPrefix || charCount - leftChars < minSuffix) continue;
    breaks.push_back(leftChars);
  }
  return breaks;
}
