/**
 * @file HyphenationCommon.cpp
 * @brief Definitions for HyphenationCommon.
 */

#include "HyphenationCommon.h"

#include <Utf8.h>

namespace {

uint32_t toLowerLatinImpl(const uint32_t cp) {
  if (cp >= 'A' && cp <= 'Z') {
    return cp - 'A' + 'a';
  }
  if ((cp >= 0x00C0 && cp <= 0x00D6) || (cp >= 0x00D8 && cp <= 0x00DE)) {
    return cp + 0x20;
  }

  switch (cp) {
    case 0x0152:
      return 0x0153;
    case 0x0178:
      return 0x00FF;
    case 0x1E9E:
      return 0x00DF;
    default:
      return cp;
  }
}

uint32_t toLowerCyrillicImpl(const uint32_t cp) {
  if (cp >= 0x0410 && cp <= 0x042F) {
    return cp + 0x20;
  }
  if (cp == 0x0401) {
    return 0x0451;
  }
  return cp;
}

uint32_t toLowerGreekImpl(const uint32_t cp) {
  if ((cp >= 0x0391 && cp <= 0x03A1) || (cp >= 0x03A3 && cp <= 0x03AB)) {
    return cp + 0x20;
  }
  switch (cp) {
    case 0x0386: return 0x03AC;  // Ά -> ά
    case 0x0388: return 0x03AD;  // Έ -> έ
    case 0x0389: return 0x03AE;  // Ή -> ή
    case 0x038A: return 0x03AF;  // Ί -> ί
    case 0x038C: return 0x03CC;  // Ό -> ό
    case 0x038E: return 0x03CD;  // Ύ -> ύ
    case 0x038F: return 0x03CE;  // Ώ -> ώ
    default: return cp;
  }
}

}

uint32_t toLowerLatin(const uint32_t cp) { return toLowerLatinImpl(cp); }

uint32_t toLowerCyrillic(const uint32_t cp) { return toLowerCyrillicImpl(cp); }

uint32_t toLowerGreek(const uint32_t cp) { return toLowerGreekImpl(cp); }

bool isLatinLetter(const uint32_t cp) {
  if ((cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z')) {
    return true;
  }

  if (((cp >= 0x00C0 && cp <= 0x00D6) || (cp >= 0x00D8 && cp <= 0x00F6) || (cp >= 0x00F8 && cp <= 0x00FF)) &&
      cp != 0x00D7 && cp != 0x00F7) {
    return true;
  }

  switch (cp) {
    case 0x0152:
    case 0x0153:
    case 0x0178:
    case 0x1E9E:
      return true;
    default:
      return false;
  }
}

bool isCyrillicLetter(const uint32_t cp) { return (cp >= 0x0400 && cp <= 0x052F); }

bool isGreekLetter(const uint32_t cp) {
  return (cp >= 0x0386 && cp <= 0x03FF && cp != 0x0387) || (cp >= 0x1F00 && cp <= 0x1FFF);
}

bool isAlphabetic(const uint32_t cp) { return isLatinLetter(cp) || isCyrillicLetter(cp) || isGreekLetter(cp); }

bool isPunctuation(const uint32_t cp) {
  switch (cp) {
    case '-':
    case '.':
    case ',':
    case '!':
    case '?':
    case ';':
    case ':':
    case '"':
    case '\'':
    case ')':
    case '(':
    case 0x00AB:
    case 0x00BB:
    case 0x2018:
    case 0x2019:
    case 0x201C:
    case 0x201D:
    case 0x00A0:
    case '{':
    case '}':
    case '[':
    case ']':
    case '/':
    case 0x203A:
    case 0x2026:
    case 0x037E:  // Greek question mark
    case 0x0387:  // Greek ano teleia
      return true;
    default:
      return false;
  }
}

bool isAsciiDigit(const uint32_t cp) { return cp >= '0' && cp <= '9'; }

bool isExplicitHyphen(const uint32_t cp) {
  switch (cp) {
    case '-':
    case 0x00AD:
    case 0x058A:
    case 0x2010:
    case 0x2011:
    case 0x2012:
    case 0x2013:
    case 0x2014:
    case 0x2015:
    case 0x2043:
    case 0x207B:
    case 0x208B:
    case 0x2212:
    case 0x2E17:
    case 0x2E3A:
    case 0x2E3B:
    case 0xFE58:
    case 0xFE63:
    case 0xFF0D:
    case 0x005F:
    case 0x2026:
      return true;
    default:
      return false;
  }
}

bool isSoftHyphen(const uint32_t cp) { return cp == 0x00AD; }

void trimSurroundingPunctuationAndFootnote(std::vector<CodepointInfo>& cps) {
  if (cps.empty()) {
    return;
  }

  if (cps.size() >= 3) {
    int end = static_cast<int>(cps.size()) - 1;
    while (end >= 0 && isPunctuation(cps[end].value)) {
      --end;
    }
    int pos = end;
    if (pos >= 0 && isAsciiDigit(cps[pos].value)) {
      while (pos >= 0 && isAsciiDigit(cps[pos].value)) {
        --pos;
      }
      if (pos >= 0 && cps[pos].value == '[' && end - pos > 1) {
        cps.erase(cps.begin() + pos, cps.end());
      }
    }
  }

  while (!cps.empty() && isPunctuation(cps.front().value)) {
    cps.erase(cps.begin());
  }
  while (!cps.empty() && isPunctuation(cps.back().value)) {
    cps.pop_back();
  }
}

std::vector<CodepointInfo> collectCodepoints(const std::string& word) {
  std::vector<CodepointInfo> cps;
  cps.reserve(word.size());

  const unsigned char* base = reinterpret_cast<const unsigned char*>(word.c_str());
  const unsigned char* ptr = base;
  while (*ptr != 0) {
    const unsigned char* current = ptr;
    const uint32_t cp = utf8NextCodepoint(&ptr);
    cps.push_back({cp, static_cast<size_t>(current - base)});
  }

  return cps;
}
