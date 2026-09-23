#include "RtlText.h"

#include <algorithm>
#include <cstring>
#include <utility>
#include <vector>

#include "../../freeink-sdk/libs/book/FreeInkBook/include/text/arab_shaping.h"

namespace {

using freeink::book::ArabForms;

struct Unit {
  std::vector<uint32_t> codepoints;
  uint8_t level = 0;
};

bool isRtlCodepoint(const uint32_t cp) {
  return (cp >= 0x0590 && cp <= 0x08FF) || (cp >= 0xFB1D && cp <= 0xFDFF) ||
         (cp >= 0xFE70 && cp <= 0xFEFF) || (cp >= 0x10800 && cp <= 0x10FFF);
}

bool isLtrCodepoint(const uint32_t cp) {
  return (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') ||
         (cp >= 0x00C0 && cp <= 0x058F) || (cp >= 0x0900 && cp <= 0x1FFF) || cp >= 0x2E80;
}

bool isNumber(const uint32_t cp) { return cp >= '0' && cp <= '9'; }

bool isArabicJoiningCodepoint(const uint32_t cp) {
  return cp >= freeink::book::kArabJoinLo && cp <= freeink::book::kArabJoinHi;
}

bool isTransparentArabicMark(const uint32_t cp) {
  return (cp >= 0x064B && cp <= 0x065F) || cp == 0x0670 || (cp >= 0x06D6 && cp <= 0x06ED) ||
         (cp >= 0x08D3 && cp <= 0x08FF);
}

bool isCombiningMark(const uint32_t cp) {
  return isTransparentArabicMark(cp) || (cp >= 0x0591 && cp <= 0x05C7);
}

uint32_t decodeUtf8(const char* text, size_t length, size_t& offset) {
  if (offset >= length) return 0;
  const uint8_t first = static_cast<uint8_t>(text[offset++]);
  if (first < 0x80) return first;
  if ((first & 0xE0) == 0xC0 && offset < length) {
    const uint8_t second = static_cast<uint8_t>(text[offset++]);
    return ((first & 0x1F) << 6) | (second & 0x3F);
  }
  if ((first & 0xF0) == 0xE0 && offset + 1 < length) {
    const uint8_t second = static_cast<uint8_t>(text[offset++]);
    const uint8_t third = static_cast<uint8_t>(text[offset++]);
    return ((first & 0x0F) << 12) | ((second & 0x3F) << 6) | (third & 0x3F);
  }
  if ((first & 0xF8) == 0xF0 && offset + 2 < length) {
    const uint8_t second = static_cast<uint8_t>(text[offset++]);
    const uint8_t third = static_cast<uint8_t>(text[offset++]);
    const uint8_t fourth = static_cast<uint8_t>(text[offset++]);
    return ((first & 0x07) << 18) | ((second & 0x3F) << 12) | ((third & 0x3F) << 6) | (fourth & 0x3F);
  }
  return 0xFFFD;
}

void appendUtf8(std::string& output, const uint32_t cp) {
  if (cp <= 0x7F) {
    output.push_back(static_cast<char>(cp));
  } else if (cp <= 0x7FF) {
    output.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    output.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp <= 0xFFFF) {
    output.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    output.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    output.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    output.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    output.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    output.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    output.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

uint8_t arabJoin(const uint32_t cp) {
  if (!isArabicJoiningCodepoint(cp)) return freeink::book::kJoinU;
  return freeink::book::kArabJoinType[cp - freeink::book::kArabJoinLo];
}

const ArabForms* arabFormsFor(const uint32_t cp) {
  uint32_t low = 0;
  uint32_t high = freeink::book::kArabFormsCount;
  while (low < high) {
    const uint32_t middle = (low + high) / 2;
    if (freeink::book::kArabForms[middle].base < cp) {
      low = middle + 1;
    } else {
      high = middle;
    }
  }
  if (low < freeink::book::kArabFormsCount && freeink::book::kArabForms[low].base == cp) {
    return &freeink::book::kArabForms[low];
  }
  return nullptr;
}

uint32_t arabPresentation(const uint32_t cp, const uint8_t form) {
  const ArabForms* forms = arabFormsFor(cp);
  if (!forms || form < 1 || form > 4) return 0;
  return forms->forms[form - 1];
}

std::vector<uint32_t> shapeArabic(const std::vector<uint32_t>& input) {
  std::vector<uint32_t> output = input;
  bool previousJoinsForward = false;

  for (size_t i = 0; i < input.size(); ++i) {
    const uint32_t cp = input[i];
    const uint8_t joinType = arabJoin(cp);
    if (joinType == freeink::book::kJoinT) continue;

    const ArabForms* forms = arabFormsFor(cp);
    if (!forms) {
      previousJoinsForward = joinType == freeink::book::kJoinC || joinType == freeink::book::kJoinD ||
                             joinType == freeink::book::kJoinL;
      continue;
    }

    bool nextAcceptsJoin = false;
    for (size_t j = i + 1; j < input.size(); ++j) {
      const uint8_t nextType = arabJoin(input[j]);
      if (nextType == freeink::book::kJoinT) continue;
      nextAcceptsJoin = nextType == freeink::book::kJoinD || nextType == freeink::book::kJoinR ||
                        nextType == freeink::book::kJoinC;
      break;
    }

    const bool joinsPrevious = previousJoinsForward &&
                               (joinType == freeink::book::kJoinD || joinType == freeink::book::kJoinR);
    const bool joinsNext = joinType == freeink::book::kJoinD && nextAcceptsJoin;
    const uint8_t form = joinsPrevious && joinsNext ? 4 : joinsPrevious ? 2 : joinsNext ? 3 : 1;
    if (const uint32_t shaped = arabPresentation(cp, form)) output[i] = shaped;
    previousJoinsForward = joinType == freeink::book::kJoinD;
  }
  return output;
}

uint32_t mirrored(const uint32_t cp) {
  switch (cp) {
    case '(': return ')';
    case ')': return '(';
    case '[': return ']';
    case ']': return '[';
    case '{': return '}';
    case '}': return '{';
    case '<': return '>';
    case '>': return '<';
    case 0x00AB: return 0x00BB;
    case 0x00BB: return 0x00AB;
    default: return cp;
  }
}

}  // namespace

namespace RtlText {

Direction firstStrongDirection(const char* text) {
  if (!text) return Direction::NONE;
  const size_t length = strlen(text);
  size_t offset = 0;
  while (offset < length) {
    const uint32_t cp = decodeUtf8(text, length, offset);
    if (isRtlCodepoint(cp)) return Direction::RTL;
    if (isLtrCodepoint(cp)) return Direction::LTR;
  }
  return Direction::NONE;
}

const char* prepareForRender(const char* text, std::string& outputStorage) {
  if (!text || *text == '\0') return text;

  const size_t length = strlen(text);
  std::vector<uint32_t> codepoints;
  codepoints.reserve(length);
  size_t offset = 0;
  Direction baseDirection = Direction::NONE;
  bool hasRtl = false;
  while (offset < length) {
    const uint32_t cp = decodeUtf8(text, length, offset);
    codepoints.push_back(cp);
    if (baseDirection == Direction::NONE) {
      if (isRtlCodepoint(cp)) {
        baseDirection = Direction::RTL;
      } else if (isLtrCodepoint(cp)) {
        baseDirection = Direction::LTR;
      }
    }
    hasRtl = hasRtl || isRtlCodepoint(cp);
  }
  if (!hasRtl || baseDirection == Direction::NONE) return text;

  codepoints = shapeArabic(codepoints);

  std::vector<Unit> units;
  units.reserve(codepoints.size());
  for (const uint32_t cp : codepoints) {
    if (isCombiningMark(cp) && !units.empty()) {
      units.back().codepoints.push_back(cp);
    } else {
      Unit unit;
      unit.codepoints.push_back(cp);
      units.push_back(std::move(unit));
    }
  }

  const uint8_t baseLevel = baseDirection == Direction::RTL ? 1 : 0;
  uint8_t previousLevel = baseLevel;
  std::vector<uint8_t> rawLevels(units.size(), 0xFF);
  uint8_t maxLevel = baseLevel;
  for (size_t i = 0; i < units.size(); ++i) {
    const uint32_t cp = units[i].codepoints.front();
    uint8_t level = 0xFF;
    if (isRtlCodepoint(cp)) {
      level = 1;
    } else if (isLtrCodepoint(cp) || isNumber(cp)) {
      level = baseLevel == 0 ? 0 : 2;
    }
    rawLevels[i] = level;
    if (level != 0xFF) {
      previousLevel = level;
      maxLevel = std::max(maxLevel, level);
    }
  }

  for (size_t i = 0; i < units.size();) {
    if (rawLevels[i] != 0xFF) {
      ++i;
      continue;
    }
    size_t end = i;
    while (end < units.size() && rawLevels[end] == 0xFF) ++end;
    uint8_t nextLevel = baseLevel;
    if (end < units.size()) nextLevel = rawLevels[end];
    const uint8_t fill = previousLevel == nextLevel ? previousLevel : baseLevel;
    for (size_t j = i; j < end; ++j) rawLevels[j] = fill;
    previousLevel = fill;
    i = end;
  }

  for (size_t i = 0; i < units.size(); ++i) units[i].level = rawLevels[i];

  std::vector<size_t> order(units.size());
  for (size_t i = 0; i < order.size(); ++i) order[i] = i;
  uint8_t minOdd = 0xFF;
  for (const Unit& unit : units) {
    if ((unit.level & 1u) != 0) minOdd = std::min(minOdd, unit.level);
  }
  if (minOdd != 0xFF) {
    for (int level = maxLevel; level >= minOdd; --level) {
      size_t i = 0;
      while (i < order.size()) {
        if (units[order[i]].level < level) {
          ++i;
          continue;
        }
        size_t end = i;
        while (end < order.size() && units[order[end]].level >= level) ++end;
        std::reverse(order.begin() + static_cast<std::ptrdiff_t>(i),
                     order.begin() + static_cast<std::ptrdiff_t>(end));
        i = end;
      }
    }
  }

  outputStorage.clear();
  outputStorage.reserve(length + 8);
  for (const size_t index : order) {
    const Unit& unit = units[index];
    for (uint32_t cp : unit.codepoints) appendUtf8(outputStorage, (unit.level & 1u) ? mirrored(cp) : cp);
  }
  return outputStorage.c_str();
}

}  // namespace RtlText
