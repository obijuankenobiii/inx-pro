/**
 * @file Utf8.cpp
 * @brief Definitions for Utf8.
 */

#include "Utf8.h"

int utf8CodepointLen(const unsigned char c) {
  if (c < 0x80) return 1;
  if ((c >> 5) == 0x6) return 2;
  if ((c >> 4) == 0xE) return 3;
  if ((c >> 3) == 0x1E) return 4;
  return 1;
}

uint32_t utf8NextCodepoint(const unsigned char** string) {
  if (**string == 0) {
    return 0;
  }

  const int bytes = utf8CodepointLen(**string);
  const uint8_t* chr = *string;
  *string += bytes;

  if (bytes == 1) {
    return chr[0];
  }

  uint32_t cp = chr[0] & ((1 << (7 - bytes)) - 1);

  for (int i = 1; i < bytes; i++) {
    cp = (cp << 6) | (chr[i] & 0x3F);
  }

  return cp;
}

void utf8NormalizeReaderText(std::string& text) {
  if (text.empty()) return;

  std::string normalized;
  normalized.reserve(text.size());
  size_t index = 0;
  while (index < text.size()) {
    const unsigned char c0 = static_cast<unsigned char>(text[index]);

    // Soft hyphen: EPUBs use this as a layout hint, not visible text.
    if (c0 == 0xC2 && index + 1 < text.size() && static_cast<unsigned char>(text[index + 1]) == 0xAD) {
      index += 2;
      continue;
    }

    // Zero-width formatting characters commonly emitted by EPUB converters.
    if (c0 == 0xE2 && index + 2 < text.size() && static_cast<unsigned char>(text[index + 1]) == 0x80 &&
        (static_cast<unsigned char>(text[index + 2]) == 0x8B ||
         static_cast<unsigned char>(text[index + 2]) == 0x8C ||
         static_cast<unsigned char>(text[index + 2]) == 0x8D)) {
      index += 3;
      continue;
    }
    if (c0 == 0xE2 && index + 2 < text.size() && static_cast<unsigned char>(text[index + 1]) == 0x81 &&
        static_cast<unsigned char>(text[index + 2]) == 0xA0) {
      index += 3;
      continue;
    }
    if (c0 == 0xEF && index + 2 < text.size() && static_cast<unsigned char>(text[index + 1]) == 0xBB &&
        static_cast<unsigned char>(text[index + 2]) == 0xBF) {
      index += 3;
      continue;
    }

    // U+FB00..U+FB06 presentation ligatures. Keep the expanded ASCII text
    // so regular and bold font runs use the same glyph sequence.
    if (c0 == 0xEF && index + 2 < text.size() && static_cast<unsigned char>(text[index + 1]) == 0xAC) {
      switch (static_cast<unsigned char>(text[index + 2])) {
        case 0x80:
          normalized += "ff";
          index += 3;
          continue;
        case 0x81:
          normalized += "fi";
          index += 3;
          continue;
        case 0x82:
          normalized += "fl";
          index += 3;
          continue;
        case 0x83:
          normalized += "ffi";
          index += 3;
          continue;
        case 0x84:
          normalized += "ffl";
          index += 3;
          continue;
        case 0x85:
        case 0x86:
          normalized += "st";
          index += 3;
          continue;
        default:
          break;
      }
    }

    normalized.push_back(static_cast<char>(c0));
    ++index;
  }

  text.swap(normalized);
}

size_t utf8RemoveLastChar(std::string& str) {
  if (str.empty()) return 0;
  size_t pos = str.size() - 1;
  while (pos > 0 && (static_cast<unsigned char>(str[pos]) & 0xC0) == 0x80) {
    --pos;
  }
  str.resize(pos);
  return pos;
}

void utf8TruncateChars(std::string& str, const size_t numChars) {
  for (size_t i = 0; i < numChars && !str.empty(); ++i) {
    utf8RemoveLastChar(str);
  }
}

void utf8TruncateBytes(std::string& str, const size_t maxBytes) {
  if (str.size() <= maxBytes) return;
  size_t pos = maxBytes;
  while (pos > 0 && (static_cast<unsigned char>(str[pos]) & 0xC0) == 0x80) {
    --pos;
  }
  str.resize(pos);
}
