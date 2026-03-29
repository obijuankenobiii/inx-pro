/**
 * @file PdfFont.cpp
 * @brief Definitions for PdfFont.
 */

#include "PdfFont.h"

#include <Fonts.h>

#include <cctype>
#include <cmath>
#include <cstring>

#include "PdfDocument.h"
#include "PdfLexer.h"
#include "PdfObject.h"

namespace {

// PDF WinAnsiEncoding (PDF32000 Appendix D.2, identical to Windows code page 1252): ASCII range and the
// Latin-1 supplement (0xA0-0xFF) map directly to their code value; only the 0x80-0x9F block differs from
// Latin-1 (curly quotes, dashes, etc.). Undefined slots map to 0 (not renderable).
uint32_t winAnsiToUnicode(const uint8_t code) {
  static const uint32_t kHighBlock[32] = {
      0x20AC, 0,      0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,  // 80-87
      0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0,      0x017D, 0,       // 88-8F
      0,      0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,  // 90-97
      0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0,      0x017E, 0x0178,  // 98-9F
  };
  if (code < 0x20) return 0;                     // control chars, not rendered
  if (code == 0x7F) return 0;                    // DEL
  if (code >= 0x80 && code <= 0x9F) return kHighBlock[code - 0x80];
  return code;  // 0x20-0x7E ASCII and 0xA0-0xFF Latin-1 supplement both map 1:1
}

uint32_t glyphNameToUnicode(const std::string& name) {
  static const std::pair<const char*, uint32_t> kCommon[] = {
      {"bullet", 0x2022},        {"endash", 0x2013},        {"emdash", 0x2014},
      {"quoteleft", 0x2018},     {"quoteright", 0x2019},    {"quotedblleft", 0x201C},
      {"quotedblright", 0x201D}, {"quotesinglbase", 0x201A},{"quotedblbase", 0x201E},
      {"ellipsis", 0x2026},      {"fi", 0xFB01},            {"fl", 0xFB02},
      {"dagger", 0x2020},        {"daggerdbl", 0x2021},     {"trademark", 0x2122},
      {"space", 0x0020},         {"hyphen", 0x002D},        {"periodcentered", 0x00B7},
      {"copyright", 0x00A9},     {"registered", 0x00AE},    {"degree", 0x00B0},
  };
  for (const auto& entry : kCommon) {
    if (name == entry.first) return entry.second;
  }
  if (name.size() == 1) return static_cast<uint32_t>(static_cast<unsigned char>(name[0]));
  return 0;
}

EpdFontFamily::Style styleFromBaseFontName(const std::string& name) {
  auto contains = [&](const char* needle) {
    std::string lower = name;
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return lower.find(needle) != std::string::npos;
  };
  const bool bold = contains("bold");
  const bool italic = contains("italic") || contains("oblique");
  if (bold && italic) return EpdFontFamily::BOLD_ITALIC;
  if (bold) return EpdFontFamily::BOLD;
  if (italic) return EpdFontFamily::ITALIC;
  return EpdFontFamily::REGULAR;
}

uint32_t bytesToUint(const std::string& bytes) {
  uint32_t v = 0;
  for (const unsigned char b : bytes) v = (v << 8) | b;
  return v;
}

// Decodes the first UTF-16BE codepoint (handling one surrogate pair) from a /ToUnicode destination string.
// Multi-codepoint ligature mappings (e.g. "ffi" -> 3 codepoints) collapse to their first codepoint - an
// acceptable approximation given we're substituting a system font's glyphs anyway, not the original ones.
uint32_t firstUtf16BECodepoint(const std::string& bytes) {
  if (bytes.size() < 2) return 0;
  const uint32_t unit0 = (static_cast<unsigned char>(bytes[0]) << 8) | static_cast<unsigned char>(bytes[1]);
  if (unit0 >= 0xD800 && unit0 <= 0xDBFF && bytes.size() >= 4) {
    const uint32_t unit1 = (static_cast<unsigned char>(bytes[2]) << 8) | static_cast<unsigned char>(bytes[3]);
    if (unit1 >= 0xDC00 && unit1 <= 0xDFFF) {
      return 0x10000 + ((unit0 - 0xD800) << 10) + (unit1 - 0xDC00);
    }
  }
  return unit0;
}

// Parses a /ToUnicode CMap stream (PostScript-ish syntax: bfchar/bfrange sections of hex strings) using the
// same lexer as PDF object syntax - CMap hex strings/numbers/keywords are byte-for-byte the same grammar.
void parseToUnicodeCMap(const std::vector<uint8_t>& bytes, std::map<uint32_t, uint32_t>& out) {
  if (bytes.empty()) return;
  PdfLexer lexer(bytes.data(), bytes.size());

  while (true) {
    const PdfToken tok = lexer.next();
    if (tok.type == PdfTokenType::Eof) break;
    if (tok.type != PdfTokenType::Keyword) continue;

    if (tok.text == "beginbfchar") {
      while (true) {
        const PdfToken src = lexer.next();
        if (src.type == PdfTokenType::Keyword && src.text == "endbfchar") break;
        if (src.type == PdfTokenType::Eof) return;
        if (src.type != PdfTokenType::StringLiteral) continue;
        const PdfToken dst = lexer.next();
        if (dst.type != PdfTokenType::StringLiteral) continue;
        out[bytesToUint(src.text)] = firstUtf16BECodepoint(dst.text);
      }
    } else if (tok.text == "beginbfrange") {
      while (true) {
        const PdfToken lo = lexer.next();
        if (lo.type == PdfTokenType::Keyword && lo.text == "endbfrange") break;
        if (lo.type == PdfTokenType::Eof) return;
        if (lo.type != PdfTokenType::StringLiteral) continue;
        const PdfToken hi = lexer.next();
        const PdfToken dst = lexer.next();
        if (hi.type != PdfTokenType::StringLiteral) continue;

        const uint32_t loCode = bytesToUint(lo.text);
        const uint32_t hiCode = bytesToUint(hi.text);
        if (hiCode < loCode || hiCode - loCode > 65536) continue;  // malformed/runaway guard

        if (dst.type == PdfTokenType::StringLiteral) {
          const uint32_t base = firstUtf16BECodepoint(dst.text);
          for (uint32_t code = loCode; code <= hiCode; code++) out[code] = base + (code - loCode);
        } else if (dst.type == PdfTokenType::ArrayStart) {
          uint32_t code = loCode;
          while (true) {
            const PdfToken item = lexer.next();
            if (item.type == PdfTokenType::ArrayEnd || item.type == PdfTokenType::Eof) break;
            if (item.type == PdfTokenType::StringLiteral && code <= hiCode) {
              out[code] = firstUtf16BECodepoint(item.text);
            }
            code++;
          }
        }
      }
    }
  }
}

// Parses a CIDFont /W array: entries are `cidFirst [w0 w1 w2 ...]` (consecutive CIDs from cidFirst) or
// `cidFirst cidLast w` (every CID in the inclusive range gets width w).
void parseCidWidths(const PdfDocument& doc, const PdfObject& wArray, std::map<uint32_t, int>& out) {
  const auto& items = wArray.arrValue;
  size_t i = 0;
  while (i < items.size()) {
    const PdfObject first = doc.resolve(items[i]);
    if (!first.isNumber() || i + 1 >= items.size()) break;
    const uint32_t cidFirst = static_cast<uint32_t>(first.asInt());
    const PdfObject second = doc.resolve(items[i + 1]);

    if (second.isArray()) {
      uint32_t cid = cidFirst;
      for (const auto& w : second.arrValue) out[cid++] = doc.resolve(w).asInt(1000);
      i += 2;
    } else if (second.isNumber() && i + 2 < items.size()) {
      const uint32_t cidLast = static_cast<uint32_t>(second.asInt());
      const int width = doc.resolve(items[i + 2]).asInt(1000);
      if (cidLast >= cidFirst && cidLast - cidFirst <= 65536) {
        for (uint32_t cid = cidFirst; cid <= cidLast; cid++) out[cid] = width;
      }
      i += 3;
    } else {
      break;  // malformed
    }
  }
}

PdfFontInfo resolveType0(const PdfDocument& doc, const PdfObject& fontDict) {
  PdfFontInfo info;
  info.isCID = true;

  const PdfObject* encodingObj = fontDict.find("Encoding");
  if (!encodingObj) return info;
  const PdfObject encoding = doc.resolve(*encodingObj);
  if (!encoding.isName() || (encoding.strValue != "Identity-H" && encoding.strValue != "Identity-V")) {
    return info;  // other CMaps (non-Identity) are out of scope
  }

  std::string baseFont;
  if (const PdfObject* baseFontObj = fontDict.find("BaseFont")) {
    const PdfObject resolved = doc.resolve(*baseFontObj);
    if (resolved.isName()) baseFont = resolved.strValue;
  }
  info.style = styleFromBaseFontName(baseFont);

  const PdfObject* descendantsObj = fontDict.find("DescendantFonts");
  if (descendantsObj) {
    const PdfObject descendants = doc.resolve(*descendantsObj);
    if (descendants.isArray() && !descendants.arrValue.empty()) {
      const PdfObject cidFont = doc.resolve(descendants.arrValue[0]);
      if (const PdfObject* dw = cidFont.find("DW")) {
        info.cidDefaultWidth = doc.resolve(*dw).asInt(1000);
      }
      if (const PdfObject* w = cidFont.find("W")) {
        const PdfObject wArray = doc.resolve(*w);
        if (wArray.isArray()) parseCidWidths(doc, wArray, info.cidWidths);
      }
    }
  }

  if (const PdfObject* toUnicodeObj = fontDict.find("ToUnicode")) {
    const PdfObject toUnicode = doc.resolve(*toUnicodeObj);
    if (toUnicode.isStream()) {
      std::vector<uint8_t> cmapBytes;
      if (doc.getStreamBytes(toUnicode, cmapBytes)) {
        parseToUnicodeCMap(cmapBytes, info.cidToUnicode);
      }
    }
  }
  // No /ToUnicode: cidToUnicode stays empty, and unicodeForCid() falls back to treating the CID itself as
  // the codepoint - correct for the common case of a Latin-subset CIDFont built with CID == Unicode.

  info.supported = true;
  return info;
}

}  // namespace

PdfFontInfo PdfFont::resolve(const PdfDocument& doc, const PdfObject& fontDict) {
  PdfFontInfo info;
  if (!fontDict.isDict()) return info;

  const PdfObject* subtype = fontDict.find("Subtype");
  if (subtype && subtype->isName() && subtype->strValue == "Type0") {
    return resolveType0(doc, fontDict);
  }
  if (subtype && subtype->isName() && subtype->strValue == "Type3") {
    return info;  // procedural glyphs, not applicable to a system-font fallback
  }

  // Simple font (Type1/TrueType/MMType1). We never decode the PDF's own outlines (embedded or not) - only
  // its text metadata - so /FontFile*, if present, doesn't affect whether the font is supported.
  std::string baseFont;
  if (const PdfObject* baseFontObj = fontDict.find("BaseFont")) {
    const PdfObject resolved = doc.resolve(*baseFontObj);
    if (resolved.isName()) baseFont = resolved.strValue;
  }
  // Strip a subset tag prefix like "ABCDEF+Helvetica-Bold".
  if (baseFont.size() > 7 && baseFont[6] == '+') {
    bool allUpper = true;
    for (int i = 0; i < 6; i++) {
      if (!std::isupper(static_cast<unsigned char>(baseFont[i]))) allUpper = false;
    }
    if (allUpper) baseFont = baseFont.substr(7);
  }
  info.style = styleFromBaseFontName(baseFont);

  for (uint16_t code = 0; code < 256; code++) {
    info.codeToUnicode[code] = winAnsiToUnicode(static_cast<uint8_t>(code));
  }

  if (const PdfObject* encodingObj = fontDict.find("Encoding")) {
    const PdfObject encoding = doc.resolve(*encodingObj);
    const PdfObject* differences = encoding.isDict() ? encoding.find("Differences") : nullptr;
    if (differences) {
      const PdfObject diffArray = doc.resolve(*differences);
      if (diffArray.isArray()) {
        int nextCode = 0;
        for (const auto& item : diffArray.arrValue) {
          if (item.isNumber()) {
            nextCode = item.asInt();
          } else if (item.isName() && nextCode >= 0 && nextCode < 256) {
            const uint32_t cp = glyphNameToUnicode(item.strValue);
            if (cp != 0) info.codeToUnicode[static_cast<size_t>(nextCode)] = cp;
            nextCode++;
          }
        }
      }
    }
  }

  info.firstChar = 0;
  if (const PdfObject* firstCharObj = fontDict.find("FirstChar")) {
    info.firstChar = doc.resolve(*firstCharObj).asInt(0);
  }
  if (const PdfObject* widthsObj = fontDict.find("Widths")) {
    const PdfObject widths = doc.resolve(*widthsObj);
    if (widths.isArray()) {
      info.widths.reserve(widths.arrValue.size());
      for (const auto& w : widths.arrValue) {
        info.widths.push_back(doc.resolve(w).asInt(info.defaultWidth));
      }
    }
  }

  info.supported = true;
  return info;
}

int PdfFont::widthForCode(const PdfFontInfo& font, const uint8_t code) {
  const int idx = static_cast<int>(code) - font.firstChar;
  if (idx >= 0 && idx < static_cast<int>(font.widths.size())) {
    return font.widths[static_cast<size_t>(idx)];
  }
  return font.defaultWidth;
}

int PdfFont::widthForCid(const PdfFontInfo& font, const uint32_t cid) {
  const auto it = font.cidWidths.find(cid);
  return it != font.cidWidths.end() ? it->second : font.cidDefaultWidth;
}

uint32_t PdfFont::unicodeForCid(const PdfFontInfo& font, const uint32_t cid) {
  const auto it = font.cidToUnicode.find(cid);
  return it != font.cidToUnicode.end() ? it->second : cid;
}

int PdfFont::nearestBuiltinFontId(const double devicePixelSize) {
  static const std::pair<int, int> kSizes[] = {
      {8, ATKINSON_HYPERLEGIBLE_8_FONT_ID},   {10, ATKINSON_HYPERLEGIBLE_10_FONT_ID},
      {12, ATKINSON_HYPERLEGIBLE_12_FONT_ID}, {14, ATKINSON_HYPERLEGIBLE_14_FONT_ID},
      {16, ATKINSON_HYPERLEGIBLE_16_FONT_ID}, {18, ATKINSON_HYPERLEGIBLE_18_FONT_ID},
  };
  int best = kSizes[0].second;
  double bestDelta = 1e9;
  for (const auto& entry : kSizes) {
    const double delta = std::abs(static_cast<double>(entry.first) - devicePixelSize);
    if (delta < bestDelta) {
      bestDelta = delta;
      best = entry.second;
    }
  }
  return best;
}

void PdfFont::appendUtf8(std::string& out, const uint32_t codepoint) {
  if (codepoint == 0) return;
  if (codepoint < 0x80) {
    out.push_back(static_cast<char>(codepoint));
  } else if (codepoint < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else if (codepoint < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  }
}
