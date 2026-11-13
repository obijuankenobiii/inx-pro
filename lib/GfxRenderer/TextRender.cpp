#include "TextRender.h"

#include <Utf8.h>

#include <algorithm>
#include <string>

#include "GfxRenderer.h"
#include "BitmapUtil.h"
#include "RtlText.h"
#include "system/FontManager.h"
#include "system/LanguageManager.h"

namespace {

constexpr uint8_t kSmallCapsScalePct = 77;
constexpr int kScaleRoundBias = 50;

bool isAsciiLower(const uint32_t cp) { return cp >= 'a' && cp <= 'z'; }

uint32_t toAsciiUpper(const uint32_t cp) { return isAsciiLower(cp) ? (cp - ('a' - 'A')) : cp; }

void appendUtf8Codepoint(std::string& out, const uint32_t cp) {
  if (cp <= 0x7F) {
    out.push_back(static_cast<char>(cp));
  } else if (cp <= 0x7FF) {
    out.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp <= 0xFFFF) {
    out.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

std::string toUpperUtf8(const char* text) {
  std::string upper;
  if (!text) {
    return upper;
  }
  const uint8_t* ptr = reinterpret_cast<const uint8_t*>(text);
  while (const uint32_t cp = utf8NextCodepoint(&ptr)) {
    appendUtf8Codepoint(upper, toAsciiUpper(cp));
  }
  return upper;
}

int scaleMetricRound(const int value, const uint8_t scalePct) {
  return std::max(1, (value * static_cast<int>(scalePct) + kScaleRoundBias) / 100);
}

// A few converted Arabic font tables contain visible glyphs with a zero
// advance. Treat those as a normal glyph-width advance; otherwise every
// following Arabic glyph is painted at the same cursor position.
int glyphAdvanceX(const EpdGlyph& glyph) {
  return glyph.advanceX > 0 ? glyph.advanceX : static_cast<int>(glyph.width);
}

uint8_t mediumTextCodeForLevel(const uint8_t level) {
  return mediumTextCodeTable()[level & 3u];
}

uint8_t imageLevelFromFontBmpVal(const uint8_t bmpVal) {
  switch (bmpVal & 3u) {
    case 0:
      return 3;
    case 1:
      return 1;
    case 2:
      return 2;
    default:
      return 0;
  }
}

bool mediumTextPlaneShouldClear(const GfxRenderer& gfx, const uint8_t level) {
  const uint8_t code = mediumTextCodeForLevel(level);
  if (gfx.getRenderMode() == GfxRenderer::GRAYSCALE_LSB) {
    return (code & 0b01u) != 0;
  }
  if (gfx.getRenderMode() == GfxRenderer::GRAYSCALE_MSB) {
    return (code & 0b10u) != 0;
  }
  return false;
}

void renderMediumTextPlanePixel(const GfxRenderer& gfx, const int x, const int y, const uint8_t level) {
  if (gfx.getRenderMode() == GfxRenderer::GRAY2_LSB) {
    if ((mapQualityGray2Level(level) & 0b01u) == 0u) {
      gfx.drawPixel(x, y, true);
    }
    return;
  }
  if (gfx.getRenderMode() == GfxRenderer::GRAY2_MSB) {
    if ((mapQualityGray2Level(level) & 0b10u) == 0u) {
      gfx.drawPixel(x, y, true);
    }
    return;
  }
  if (mediumTextPlaneShouldClear(gfx, level)) {
    gfx.drawPixel(x, y, false);
  }
}

void renderSolidTextPixel(const GfxRenderer& gfx, const int x, const int y, const bool pixelState) {
  if (gfx.getRenderMode() == GfxRenderer::BW) {
    gfx.drawPixel(x, y, pixelState);
    return;
  }
  renderMediumTextPlanePixel(gfx, x, y, pixelState ? 3 : 0);
}

bool read1BitGlyphPixel(const uint8_t* bitmap, const int width, const int height, const int x, const int y) {
  if (!bitmap || x < 0 || y < 0 || x >= width || y >= height) {
    return false;
  }
  const int pixelPosition = y * width + x;
  const uint8_t byte = bitmap[pixelPosition / 8];
  const uint8_t bitIndex = 7 - (pixelPosition % 8);
  return ((byte >> bitIndex) & 1u) != 0;
}

bool read1BitRowPixel(const uint8_t* row, const int width, const int x) {
  if (!row || x < 0 || x >= width) {
    return false;
  }
  return ((row[x / 8] >> (7 - (x % 8))) & 1u) != 0;
}

// TTF glyph bitmaps are packed as one continuous pixel stream, so a row can
// begin partway through a byte when the glyph width is not divisible by four.
uint8_t read2BitRowPixel(const uint8_t* row, const size_t rowPixelOffset, const int x) {
  const size_t packedPixel = rowPixelOffset + static_cast<size_t>(x);
  const uint8_t byte = row[packedPixel / 4u];
  return static_cast<uint8_t>((byte >> ((3u - (packedPixel % 4u)) * 2u)) & 0x3u);
}

const EpdFontFamily* findFontFamily(const GfxRenderer& gfx, const int fontId) { return gfx.findFontFamily(fontId); }

ExternalFont* findStreamingFont(const GfxRenderer& gfx, const EpdFontData* data) { return gfx.findStreamingFont(data); }

bool hasGlyph(const GfxRenderer& gfx, const EpdFontFamily& family, const uint32_t cp,
              const EpdFontFamily::Style style) {
  const EpdFontData* data = family.getData(style);
  if (!data) return false;
  if (ExternalFont* stream = findStreamingFont(gfx, data)) {
    EpdGlyph glyph{};
    return stream->getGlyphMetadata(cp, glyph);
  }
  return family.getGlyph(cp, style) != nullptr;
}

bool isOverlayMark(const uint32_t cp) {
  const bool unicodeCombining = (cp >= 0x0300 && cp <= 0x036F) ||
                                (cp >= 0x1DC0 && cp <= 0x1DFF) ||
                                (cp >= 0x20D0 && cp <= 0x20FF) ||
                                (cp >= 0xFE20 && cp <= 0xFE2F);
  return unicodeCombining || RtlText::isTransparentMark(cp);
}

struct MarkAnchorMetrics {
  int cursorX = 0;
  int left = 0;
  int width = 0;
  int top = 0;
  bool valid = false;
};

enum class MarkAnchor : uint8_t {
  CENTER_RAISED,
  CENTER_NATIVE,
  RIGHT_NATIVE,
  LEFT_NATIVE,
};

MarkAnchor markAnchorFor(const uint32_t cp) {
  switch (cp) {
    case 0x05BC:  // dagesh / mapiq / shuruk dot
    case 0x05BA:  // holam haser for vav
      return MarkAnchor::CENTER_NATIVE;
    case 0x05C1:  // shin dot
      return MarkAnchor::RIGHT_NATIVE;
    case 0x05B9:  // holam
    case 0x05C2:  // sin dot
      return MarkAnchor::LEFT_NATIVE;
    default:
      return MarkAnchor::CENTER_RAISED;
  }
}

int markAnchorShift(const MarkAnchor anchor, const int baseWidth, const int markWidth) {
  switch (anchor) {
    case MarkAnchor::LEFT_NATIVE:
      return 0;
    case MarkAnchor::RIGHT_NATIVE:
      return baseWidth - markWidth;
    default:
      return baseWidth / 2 - markWidth / 2;
  }
}

int markCursorX(const MarkAnchor anchor, const MarkAnchorMetrics& base, const EpdGlyph& mark) {
  return base.cursorX + base.left + markAnchorShift(anchor, base.width, mark.width) - mark.left;
}

int markRaise(const MarkAnchor anchor, const EpdGlyph& mark, const MarkAnchorMetrics& base) {
  if (anchor != MarkAnchor::CENTER_RAISED || mark.top - mark.height <= 0) return 0;
  const int gap = mark.top - mark.height - base.top;
  return gap < 1 ? 1 - gap : 0;
}

bool lookupGlyph(const GfxRenderer& gfx, const EpdFontFamily& family, const uint32_t cp,
                 const EpdFontFamily::Style style, EpdGlyph& storage, const EpdGlyph*& glyph,
                 const bool allowReplacement) {
  glyph = nullptr;
  const EpdFontData* data = family.getData(style);
  if (!data) return false;
  if (ExternalFont* stream = findStreamingFont(gfx, data)) {
    if (stream->getGlyphMetadata(cp, storage)) {
      glyph = &storage;
    } else if (allowReplacement && stream->getGlyphMetadata(REPLACEMENT_GLYPH, storage)) {
      glyph = &storage;
    }
  } else {
    glyph = family.getGlyph(cp, style);
    if (!glyph && allowReplacement) glyph = family.getGlyph(REPLACEMENT_GLYPH, style);
  }
  return glyph != nullptr;
}

bool containsOverlayMark(const char* text) {
  if (!text) return false;
  const uint8_t* ptr = reinterpret_cast<const uint8_t*>(text);
  while (const uint32_t cp = utf8NextCodepoint(&ptr)) {
    if (isOverlayMark(cp)) return true;
  }
  return false;
}

struct RtlCoverageContext {
  const GfxRenderer* gfx = nullptr;
  int fontId = 0;
  EpdFontFamily::Style style = EpdFontFamily::REGULAR;
};

bool rtlGlyphIsCovered(const uint32_t cp, void* context) {
  const auto* coverage = static_cast<const RtlCoverageContext*>(context);
  if (!coverage || !coverage->gfx) return false;
  const EpdFontFamily* family = findFontFamily(*coverage->gfx, coverage->fontId);
  return family && hasGlyph(*coverage->gfx, *family, cp, coverage->style);
}

const char* prepareTextForFont(const char* text, std::string& storage, const GfxRenderer& gfx,
                               const int fontId, const EpdFontFamily::Style style) {
  RtlCoverageContext coverage{&gfx, fontId, style};
  return RtlText::prepareForRender(text, storage, rtlGlyphIsCovered, &coverage);
}

int resolveFontForCodepoint(GfxRenderer& gfx, const int fontId, const uint32_t cp,
                            const EpdFontFamily::Style style) {
  const EpdFontFamily* primary = findFontFamily(gfx, fontId);
  if (!primary || hasGlyph(gfx, *primary, cp, style) || cp < 0x80 || cp == 0) return fontId;

  int preferredPt = 14;
  if (const FontManager::FontInfo* info = FontManager::getFontInfo(fontId)) {
    preferredPt = info->size;
  }
  const int fallbackId = FontManager::findLanguageFontForCodepoint(cp, preferredPt, style, gfx,
                                                                    LanguageManager::bookLanguageCode());
  return fallbackId != 0 ? fallbackId : fontId;
}

bool embeddedGlyphBitmapIsValid(const EpdFontData* fontData, const EpdGlyph* glyph) {
  if (!fontData || !fontData->bitmap || !glyph || fontData->bitmapSize == 0) {
    return false;
  }
  const size_t pixelCount = static_cast<size_t>(glyph->width) * static_cast<size_t>(glyph->height);
  const size_t requiredBytes = fontData->is2Bit ? (pixelCount + 3u) / 4u : (pixelCount + 7u) / 8u;
  const size_t offset = glyph->dataOffset;
  const size_t length = glyph->dataLength;
  return offset <= fontData->bitmapSize && requiredBytes <= fontData->bitmapSize - offset && length >= requiredBytes;
}

}

int TextRender::getWidth(const int fontId, const char* text, const EpdFontFamily::Style style) const {
  text = LanguageManager::translateText(text);
  return getUntranslatedWidth(fontId, text, style);
}

int TextRender::getUntranslatedWidth(const int fontId, const char* text, const EpdFontFamily::Style style) const {
  std::string visualText;
  text = prepareTextForFont(text, visualText, gfx, fontId, style);
  const EpdFontFamily* primary = findFontFamily(gfx, fontId);
  if (primary == nullptr) {
    INX_SERIAL.printf("[%lu] [GFX] Font %d not found\n", millis(), fontId);
    return 0;
  }
  if (!text || *text == '\0') return 0;

  // Streaming TTF fonts already expose cached glyph metadata. Measure the
  // common no-fallback case in one pass; the old path first scanned every
  // codepoint for fallback glyphs and then scanned the same text again for
  // its width, doubling the hot-path work during EPUB pagination.
  if (ExternalFont* stream = findStreamingFont(gfx, primary->getData(style))) {
    int streamingWidth = 0;
    int streamingInkRight = 0;
    bool needsFallback = false;
    const uint8_t* streamPtr = reinterpret_cast<const uint8_t*>(text);
    while (const uint32_t cp = utf8NextCodepoint(&streamPtr)) {
      if (isOverlayMark(cp)) continue;
      EpdGlyph glyph{};
      if (stream->getGlyphMetadata(cp, glyph)) {
        streamingInkRight = std::max(streamingInkRight, streamingWidth + glyph.left + glyph.width);
        streamingWidth += glyphAdvanceX(glyph);
        continue;
      }

      const int resolved = resolveFontForCodepoint(gfx, fontId, cp, style);
      if (resolved != fontId) {
        needsFallback = true;
        break;
      }
      if (stream->getGlyphMetadata(REPLACEMENT_GLYPH, glyph)) {
        streamingInkRight = std::max(streamingInkRight, streamingWidth + glyph.left + glyph.width);
        streamingWidth += glyphAdvanceX(glyph);
      }
    }
    if (!needsFallback) {
      return std::max(streamingWidth, streamingInkRight);
    }
  }

  // Use the same cursor-advance model as the renderer for every font. The
  // family bounding-box helper measures ink extents, not the distance the
  // cursor advances. That difference is especially visible with Arabic
  // joining forms and their overhangs: a line can be measured too narrowly,
  // causing the next word to overlap and RTL margins to drift.
  int totalWidth = 0;
  int inkRight = 0;
  const uint8_t* ptr = reinterpret_cast<const uint8_t*>(text);
  while (const uint32_t cp = utf8NextCodepoint(&ptr)) {
    if (isOverlayMark(cp)) continue;
    const int resolvedId = resolveFontForCodepoint(gfx, fontId, cp, style);
    const EpdFontFamily* family = findFontFamily(gfx, resolvedId);
    if (!family) continue;
    EpdGlyph glyphStorage{};
    const EpdGlyph* glyph = nullptr;
    const EpdFontData* data = family->getData(style);
    if (ExternalFont* stream = findStreamingFont(gfx, data)) {
      if (stream->getGlyphMetadata(cp, glyphStorage)) glyph = &glyphStorage;
      else if (stream->getGlyphMetadata(REPLACEMENT_GLYPH, glyphStorage)) glyph = &glyphStorage;
    } else {
      glyph = family->getGlyph(cp, style);
      if (!glyph) glyph = family->getGlyph(REPLACEMENT_GLYPH, style);
    }
    if (glyph) {
      inkRight = std::max(inkRight, totalWidth + glyph->left + glyph->width);
      totalWidth += glyphAdvanceX(*glyph);
    }
  }
  return std::max(totalWidth, inkRight);
}

int TextRender::getHeight(const int fontId) const { return getLineHeight(fontId); }

int TextRender::getFontAscenderSize(const int fontId) const {
  if (findFontFamily(gfx, fontId) == nullptr) {
    INX_SERIAL.printf("[%lu] [GFX] Font %d not found\n", millis(), fontId);
    return 0;
  }
  return (*findFontFamily(gfx, fontId)).getData(EpdFontFamily::REGULAR)->ascender;
}

int TextRender::getGlyphTopInset(const int fontId, const uint32_t cp, const EpdFontFamily::Style style) const {
  const int resolvedId = resolveFontForCodepoint(gfx, fontId, cp, style);
  if (findFontFamily(gfx, resolvedId) == nullptr) {
    return 0;
  }
  const auto& family = (*findFontFamily(gfx, resolvedId));
  const EpdFontData* data = family.getData(style);
  if (!data) {
    return 0;
  }
  EpdGlyph storage;
  const EpdGlyph* glyph = nullptr;
  ExternalFont* streamIt = findStreamingFont(gfx, data);
  if (streamIt) {
    if (streamIt->getGlyphMetadata(cp, storage)) {
      glyph = &storage;
    }
  } else {
    glyph = family.getGlyph(cp, style);
  }
  if (!glyph) {
    return 0;
  }
  return data->ascender - glyph->top;
}

int TextRender::getFontTopInset(const int fontId, const EpdFontFamily::Style style) const {
  // The line box must be positioned from the glyph that reaches highest, not
  // from one arbitrary reference glyph. Keep this list to common ascenders so
  // punctuation/descenders cannot pull the whole line out of position.
  constexpr uint32_t kAscenderGlyphs[] = {'H', 'A', 'b', 'd', 'f', 'h', 'k', 'l', 't'};
  int topInset = 0;
  bool found = false;
  for (const uint32_t cp : kAscenderGlyphs) {
    const int inset = getGlyphTopInset(fontId, cp, style);
    // A zero result also means that the glyph is unavailable. Ignore it when
    // another usable glyph exists, but retain the old H-based fallback below.
    if (inset <= 0) continue;
    if (!found || inset < topInset) {
      topInset = inset;
      found = true;
    }
  }
  return found ? topInset : getGlyphTopInset(fontId, 'H', style);
}

int TextRender::getGlyphBottomInset(const int fontId, const uint32_t cp, const EpdFontFamily::Style style) const {
  const int resolvedId = resolveFontForCodepoint(gfx, fontId, cp, style);
  if (findFontFamily(gfx, resolvedId) == nullptr) {
    return 0;
  }
  const auto& family = (*findFontFamily(gfx, resolvedId));
  const EpdFontData* data = family.getData(style);
  if (!data) {
    return 0;
  }
  EpdGlyph storage;
  const EpdGlyph* glyph = nullptr;
  ExternalFont* streamIt = findStreamingFont(gfx, data);
  if (streamIt) {
    if (streamIt->getGlyphMetadata(cp, storage)) {
      glyph = &storage;
    }
  } else {
    glyph = family.getGlyph(cp, style);
  }
  if (!glyph) {
    return 0;
  }
  const int glyphBottom = data->ascender - glyph->top + glyph->height;
  return std::max(0, static_cast<int>(data->advanceY) - glyphBottom);
}

int TextRender::getLineHeight(const int fontId) const {
  if (findFontFamily(gfx, fontId) == nullptr) {
    INX_SERIAL.printf("[%lu] [GFX] Font %d not found\n", millis(), fontId);
    return 0;
  }
  return (*findFontFamily(gfx, fontId)).getData(EpdFontFamily::REGULAR)->advanceY;
}

int TextRender::getSpaceWidth(const int fontId) const {
  if (findFontFamily(gfx, fontId) == nullptr) {
    INX_SERIAL.printf("[%lu] [GFX] Font %d not found\n", millis(), fontId);
    return 0;
  }

  const EpdFontFamily& font = (*findFontFamily(gfx, fontId));
  const EpdFontData* fontData = font.getData(EpdFontFamily::REGULAR);
  if (!fontData) {
    return 0;
  }

  ExternalFont* streamIt = findStreamingFont(gfx, fontData);
  if (streamIt) {
    EpdGlyph g{};
    constexpr uint32_t kSpace = 0x20;
    if (!streamIt->getGlyphMetadata(kSpace, g)) {
      if (!streamIt->getGlyphMetadata(REPLACEMENT_GLYPH, g)) {
        return 0;
      }
    }
    return g.advanceX;
  }

  const EpdGlyph* glyph = font.getGlyph(' ', EpdFontFamily::REGULAR);
  if (!glyph) {
    glyph = font.getGlyph(REPLACEMENT_GLYPH, EpdFontFamily::REGULAR);
  }
  return glyph ? glyphAdvanceX(*glyph) : 0;
}

bool TextRender::supportsAntiAliasing(const int fontId) const {
  const EpdFontFamily* family = findFontFamily(gfx, fontId);
  if (!family) {
    return false;
  }

  const EpdFontData* regular = family->getData(EpdFontFamily::REGULAR);
  if (!regular || !regular->is2Bit) {
    return false;
  }

  ExternalFont* stream = findStreamingFont(gfx, regular);
  if (stream) {
    return stream->hasAntiAliasData();
  }

  return true;
}

int TextRender::getSmallCapsWidth(const int fontId, const char* text, const EpdFontFamily::Style style) const {
  text = LanguageManager::translateText(text);
  std::string visualText;
  text = prepareTextForFont(text, visualText, gfx, fontId, style);
  if (!text || *text == '\0' || findFontFamily(gfx, fontId) == nullptr) {
    return 0;
  }

  const std::string upper = toUpperUtf8(text);
  const char* ptr = upper.c_str();
  int totalWidth = 0;
  while (const uint32_t cp = utf8NextCodepoint(reinterpret_cast<const uint8_t**>(&ptr))) {
    if (isOverlayMark(cp)) continue;
    const int resolvedId = resolveFontForCodepoint(gfx, fontId, cp, style);
    const EpdFontFamily* family = findFontFamily(gfx, resolvedId);
    if (!family) continue;
    const EpdFontData* fontData = family->getData(style);
    if (!fontData) continue;
    ExternalFont* streamIt = findStreamingFont(gfx, fontData);
    EpdGlyph glyphStorage{};
    const EpdGlyph* glyph = nullptr;
    if (streamIt) {
      if (streamIt->getGlyphMetadata(cp, glyphStorage) || streamIt->getGlyphMetadata(REPLACEMENT_GLYPH, glyphStorage)) {
        glyph = &glyphStorage;
      }
    } else {
      glyph = family->getGlyph(cp, style);
      if (!glyph) {
        glyph = family->getGlyph(REPLACEMENT_GLYPH, style);
      }
    }
    if (!glyph) {
      continue;
    }
    totalWidth += scaleMetricRound(glyphAdvanceX(*glyph), kSmallCapsScalePct);
  }
  return totalWidth;
}

int TextRender::getScaledWidth(const int fontId, const char* text, const uint8_t scalePct,
                               const EpdFontFamily::Style style) const {
  text = LanguageManager::translateText(text);
  std::string visualText;
  text = prepareTextForFont(text, visualText, gfx, fontId, style);
  if (!text || *text == '\0' || findFontFamily(gfx, fontId) == nullptr) {
    return 0;
  }

  const uint8_t* ptr = reinterpret_cast<const uint8_t*>(text);
  int totalWidth = 0;
  while (const uint32_t cp = utf8NextCodepoint(&ptr)) {
    if (isOverlayMark(cp)) continue;
    const int resolvedId = resolveFontForCodepoint(gfx, fontId, cp, style);
    const EpdFontFamily* family = findFontFamily(gfx, resolvedId);
    if (!family) continue;
    const EpdFontData* fontData = family->getData(style);
    if (!fontData) continue;
    ExternalFont* streamIt = findStreamingFont(gfx, fontData);
    EpdGlyph glyphStorage{};
    const EpdGlyph* glyph = nullptr;
    if (streamIt) {
      if (streamIt->getGlyphMetadata(cp, glyphStorage) || streamIt->getGlyphMetadata(REPLACEMENT_GLYPH, glyphStorage)) {
        glyph = &glyphStorage;
      }
    } else {
      glyph = family->getGlyph(cp, style);
      if (!glyph) {
        glyph = family->getGlyph(REPLACEMENT_GLYPH, style);
      }
    }
    if (!glyph) {
      continue;
    }
    totalWidth += scaleMetricRound(glyphAdvanceX(*glyph), scalePct);
  }
  return totalWidth;
}

std::string TextRender::truncate(const int fontId, const char* text, const int maxWidth,
                                 const EpdFontFamily::Style style) const {
  if (!text || maxWidth <= 0) return "";

  text = LanguageManager::translateText(text);
  std::string item = text;
  const char* ellipsis = "...";
  int textWidth = getUntranslatedWidth(fontId, item.c_str(), style);
  if (textWidth <= maxWidth) {
    return item;
  }

  while (!item.empty() && getUntranslatedWidth(fontId, (item + ellipsis).c_str(), style) >= maxWidth) {
    utf8RemoveLastChar(item);
  }

  return item.empty() ? ellipsis : item + ellipsis;
}

void TextRender::rotated90CW(const int fontId, const int x, const int y, const char* text, const bool black,
                             const EpdFontFamily::Style style) const {
  text = LanguageManager::translateText(text);
  std::string visualText;
  text = prepareTextForFont(text, visualText, gfx, fontId, style);
  if (text == nullptr || *text == '\0' || findFontFamily(gfx, fontId) == nullptr) {
    return;
  }
  int yPos = y;
  MarkAnchorMetrics lastBase;

  uint32_t cp;
  while ((cp = utf8NextCodepoint(reinterpret_cast<const uint8_t**>(&text)))) {
    const int resolvedId = resolveFontForCodepoint(gfx, fontId, cp, style);
    const EpdFontFamily* resolvedFont = findFontFamily(gfx, resolvedId);
    if (!resolvedFont) continue;
    const EpdFontData* fontData = resolvedFont->getData(style);
    if (!fontData) continue;
    ExternalFont* it = findStreamingFont(gfx, fontData);
    EpdGlyph glyphStorage{};
    const EpdGlyph* glyph = nullptr;

    if (isOverlayMark(cp)) {
      if (!lastBase.valid || !lookupGlyph(gfx, *resolvedFont, cp, style, glyphStorage, glyph, false)) continue;
      const MarkAnchor anchor = markAnchorFor(cp);
      // In the rotated coordinate system the horizontal anchor becomes a
      // vertical cursor position. The mark remains zero-advance.
      int markY = lastBase.cursorX - lastBase.left - markAnchorShift(anchor, lastBase.width, glyph->width) + glyph->left;
      int markX = x;
      renderChar(*resolvedFont, cp, &markX, &markY, black, style, false);
      continue;
    }

    if (it) {
      if (it->getGlyphMetadata(cp, glyphStorage) || it->getGlyphMetadata(REPLACEMENT_GLYPH, glyphStorage)) {
        glyph = &glyphStorage;
      }
    } else {
      glyph = resolvedFont->getGlyph(cp, style);
      if (!glyph) {
        glyph = resolvedFont->getGlyph(REPLACEMENT_GLYPH, style);
      }
    }

    if (!glyph) {
      continue;
    }

    const int baseY = yPos;

    if (fontData->bitmap != nullptr && !embeddedGlyphBitmapIsValid(fontData, glyph)) {
      const EpdGlyph* replacement = resolvedFont->getGlyph(REPLACEMENT_GLYPH, style);
      if (embeddedGlyphBitmapIsValid(fontData, replacement)) {
        glyph = replacement;
      } else {
        yPos -= glyphAdvanceX(*glyph);
        continue;
      }
    }

    uint8_t localStackBuffer[1024];
    const uint8_t* bitmap = nullptr;

    if (fontData->bitmap != nullptr) {
      bitmap = &fontData->bitmap[glyph->dataOffset];
    } else if (it) {
      if (it->getGlyphBitmap(glyph->dataOffset, glyph->dataLength, localStackBuffer)) {
        bitmap = localStackBuffer;
      }
    }

    if (bitmap != nullptr) {
      const int is2Bit = fontData->is2Bit;
      for (int glyphY = 0; glyphY < glyph->height; glyphY++) {
        for (int glyphX = 0; glyphX < glyph->width; glyphX++) {
          const int pixelPosition = glyphY * glyph->width + glyphX;
          const int screenX = x + (fontData->ascender - glyph->top + glyphY);
          const int screenY = yPos - glyph->left - glyphX;

          if (is2Bit) {
            const uint8_t byte = bitmap[pixelPosition / 4];
            const uint8_t bitIndex = (3 - (pixelPosition % 4)) * 2;
            const uint8_t bmpVal = 3 - ((byte >> bitIndex) & 0x3);

            if (gfx.renderMode == GfxRenderer::BW && bmpVal < 3) {
              gfx.drawPixel(screenX, screenY, black);
            } else {
              renderMediumTextPlanePixel(gfx, screenX, screenY, imageLevelFromFontBmpVal(bmpVal));
            }
          } else {
            const bool ink = read1BitGlyphPixel(bitmap, glyph->width, glyph->height, glyphX, glyphY);
            if (ink) {
              renderSolidTextPixel(gfx, screenX, screenY, black);
            }
          }
        }
      }
    }
    lastBase = {baseY, glyph->left, glyph->width, glyph->top, true};
    yPos -= glyphAdvanceX(*glyph);
  }
}

void TextRender::render(const int fontId, const int x, const int y, const char* text, const bool black,
                        const EpdFontFamily::Style style) const {
  text = LanguageManager::translateText(text);
  renderUntranslated(fontId, x, y, text, black, style);
}

void TextRender::renderUntranslated(const int fontId, const int x, const int y, const char* text, const bool black,
                                    const EpdFontFamily::Style style) const {
  std::string visualText;
  text = prepareTextForFont(text, visualText, gfx, fontId, style);
  const int yPos = y + getFontAscenderSize(fontId);
  int xpos = x;

  if (text == nullptr || *text == '\0') {
    return;
  }

  if (findFontFamily(gfx, fontId) == nullptr) {
    INX_SERIAL.printf("[%lu] [GFX] Font %d not found\n", millis(), fontId);
    return;
  }
  uint32_t cp;
  int yCursor = yPos;
  MarkAnchorMetrics lastBase;
  while ((cp = utf8NextCodepoint(reinterpret_cast<const uint8_t**>(&text)))) {
    const int resolvedId = resolveFontForCodepoint(gfx, fontId, cp, style);
    const EpdFontFamily* resolvedFont = findFontFamily(gfx, resolvedId);
    if (!resolvedFont) continue;

    EpdGlyph glyphStorage{};
    const EpdGlyph* glyph = nullptr;
    if (isOverlayMark(cp)) {
      // Arabic harakat and Hebrew niqqud have no cursor advance. Draw them
      // over the preceding base glyph; rendering them at the current cursor
      // is what caused the visible overlap reported on Arabic pages.
      if (!lastBase.valid || !lookupGlyph(gfx, *resolvedFont, cp, style, glyphStorage, glyph, false)) continue;
      const MarkAnchor anchor = markAnchorFor(cp);
      int markX = markCursorX(anchor, lastBase, *glyph);
      int markY = yPos - markRaise(anchor, *glyph, lastBase);
      renderChar(*resolvedFont, cp, &markX, &markY, black, style, false);
      continue;
    }

    const int baseX = xpos;
    renderChar(*resolvedFont, cp, &xpos, &yCursor, black, style, false);
    if (lookupGlyph(gfx, *resolvedFont, cp, style, glyphStorage, glyph, true)) {
      lastBase = {baseX, glyph->left, glyph->width, glyph->top, true};
    } else {
      lastBase.valid = false;
    }
  }
}

void TextRender::renderGray(const int fontId, const int x, const int y, const char* text, const bool black,
                            const EpdFontFamily::Style style) const {
  text = LanguageManager::translateText(text);
  std::string visualText;
  text = prepareTextForFont(text, visualText, gfx, fontId, style);
  const int yPos = y + getFontAscenderSize(fontId);
  int xpos = x;

  if (text == nullptr || *text == '\0') {
    return;
  }
  if (findFontFamily(gfx, fontId) == nullptr) {
    INX_SERIAL.printf("[%lu] [GFX] Font %d not found\n", millis(), fontId);
    return;
  }
  uint32_t cp;
  int yCursor = yPos;
  MarkAnchorMetrics lastBase;
  while ((cp = utf8NextCodepoint(reinterpret_cast<const uint8_t**>(&text)))) {
    const int resolvedId = resolveFontForCodepoint(gfx, fontId, cp, style);
    const EpdFontFamily* resolvedFont = findFontFamily(gfx, resolvedId);
    if (!resolvedFont) continue;

    EpdGlyph glyphStorage{};
    const EpdGlyph* glyph = nullptr;
    if (isOverlayMark(cp)) {
      if (!lastBase.valid || !lookupGlyph(gfx, *resolvedFont, cp, style, glyphStorage, glyph, false)) continue;
      const MarkAnchor anchor = markAnchorFor(cp);
      int markX = markCursorX(anchor, lastBase, *glyph);
      int markY = yPos - markRaise(anchor, *glyph, lastBase);
      renderChar(*resolvedFont, cp, &markX, &markY, black, style, true);
      continue;
    }

    const int baseX = xpos;
    renderChar(*resolvedFont, cp, &xpos, &yCursor, black, style, true);
    if (lookupGlyph(gfx, *resolvedFont, cp, style, glyphStorage, glyph, true)) {
      lastBase = {baseX, glyph->left, glyph->width, glyph->top, true};
    } else {
      lastBase.valid = false;
    }
  }
}

int TextRender::renderScaled(const int fontId, const int x, const int y, const char* text, const uint8_t scalePct,
                             const bool black, const EpdFontFamily::Style style) const {
  return renderScaledInternal(fontId, x, y, text, scalePct, black, style, false);
}

int TextRender::renderScaledGray(const int fontId, const int x, const int y, const char* text,
                                 const uint8_t scalePct, const bool black, const EpdFontFamily::Style style) const {
  return renderScaledInternal(fontId, x, y, text, scalePct, black, style, true);
}

int TextRender::renderScaledInternal(const int fontId, const int x, const int y, const char* text,
                                     const uint8_t scalePct, const bool black,
                                     const EpdFontFamily::Style style, const bool gray) const {
  text = LanguageManager::translateText(text);
  std::string visualText;
  text = prepareTextForFont(text, visualText, gfx, fontId, style);
  if (text == nullptr || *text == '\0') {
    return x;
  }
  if (findFontFamily(gfx, fontId) == nullptr) {
    INX_SERIAL.printf("[%lu] [GFX] Font %d not found\n", millis(), fontId);
    return x;
  }

  const int yPos = y + getFontAscenderSize(fontId);
  int xpos = x;
  int yCursor = yPos;
  MarkAnchorMetrics lastBase;

  uint32_t cp;
  while ((cp = utf8NextCodepoint(reinterpret_cast<const uint8_t**>(&text)))) {
    const int resolvedId = resolveFontForCodepoint(gfx, fontId, cp, style);
    const EpdFontFamily* resolvedFont = findFontFamily(gfx, resolvedId);
    if (!resolvedFont) continue;

    EpdGlyph glyphStorage{};
    const EpdGlyph* glyph = nullptr;
    if (isOverlayMark(cp)) {
      if (!lastBase.valid || !lookupGlyph(gfx, *resolvedFont, cp, style, glyphStorage, glyph, false)) continue;
      const MarkAnchor anchor = markAnchorFor(cp);
      const int scaledLeft = (glyph->left * static_cast<int>(scalePct) + kScaleRoundBias) / 100;
      const int scaledWidth = std::max(1, (static_cast<int>(glyph->width) * scalePct + kScaleRoundBias) / 100);
      const int scaledTop = std::max(1, (glyph->top * static_cast<int>(scalePct) + kScaleRoundBias) / 100);
      EpdGlyph scaledMark = *glyph;
      scaledMark.left = static_cast<int16_t>(scaledLeft);
      scaledMark.width = static_cast<uint8_t>(scaledWidth);
      scaledMark.top = static_cast<int16_t>(scaledTop);
      scaledMark.height = static_cast<uint8_t>(std::max(1, (static_cast<int>(glyph->height) * scalePct + kScaleRoundBias) / 100));
      int markX = markCursorX(anchor, lastBase, scaledMark);
      int markY = yPos - markRaise(anchor, scaledMark, lastBase);
      renderScaledChar(*resolvedFont, cp, &markX, &markY, black, style, scalePct, gray);
      continue;
    }

    const int baseX = xpos;
    renderScaledChar(*resolvedFont, cp, &xpos, &yCursor, black, style, scalePct, gray);
    if (lookupGlyph(gfx, *resolvedFont, cp, style, glyphStorage, glyph, true)) {
      lastBase = {baseX,
                  (glyph->left * static_cast<int>(scalePct) + kScaleRoundBias) / 100,
                  std::max(1, (static_cast<int>(glyph->width) * scalePct + kScaleRoundBias) / 100),
                  std::max(1, (glyph->top * static_cast<int>(scalePct) + kScaleRoundBias) / 100), true};
    } else {
      lastBase.valid = false;
    }
  }
  return xpos;
}

int TextRender::renderSmallCaps(const int fontId, const int x, const int y, const char* text, const bool black,
                                const EpdFontFamily::Style style) const {
  text = LanguageManager::translateText(text);
  if (text == nullptr || *text == '\0') {
    return x;
  }
  if (findFontFamily(gfx, fontId) == nullptr) {
    INX_SERIAL.printf("[%lu] [GFX] Font %d not found\n", millis(), fontId);
    return x;
  }

  const std::string upper = toUpperUtf8(text);
  std::string visualText;
  const char* ptr = prepareTextForFont(upper.c_str(), visualText, gfx, fontId, style);
  const int yPos = y + getFontAscenderSize(fontId);
  int xpos = x;
  int yCursor = yPos;
  MarkAnchorMetrics lastBase;

  uint32_t cp;
  while ((cp = utf8NextCodepoint(reinterpret_cast<const uint8_t**>(&ptr)))) {
    const int resolvedId = resolveFontForCodepoint(gfx, fontId, cp, style);
    const EpdFontFamily* resolvedFont = findFontFamily(gfx, resolvedId);
    if (!resolvedFont) continue;

    EpdGlyph glyphStorage{};
    const EpdGlyph* glyph = nullptr;
    if (isOverlayMark(cp)) {
      if (!lastBase.valid || !lookupGlyph(gfx, *resolvedFont, cp, style, glyphStorage, glyph, false)) continue;
      const MarkAnchor anchor = markAnchorFor(cp);
      const int scaledLeft = (glyph->left * static_cast<int>(kSmallCapsScalePct) + kScaleRoundBias) / 100;
      const int scaledWidth = std::max(1, (static_cast<int>(glyph->width) * kSmallCapsScalePct + kScaleRoundBias) / 100);
      const int scaledTop = std::max(1, (glyph->top * static_cast<int>(kSmallCapsScalePct) + kScaleRoundBias) / 100);
      EpdGlyph scaledMark = *glyph;
      scaledMark.left = static_cast<int16_t>(scaledLeft);
      scaledMark.width = static_cast<uint8_t>(scaledWidth);
      scaledMark.top = static_cast<int16_t>(scaledTop);
      scaledMark.height = static_cast<uint8_t>(std::max(1, (static_cast<int>(glyph->height) * kSmallCapsScalePct + kScaleRoundBias) / 100));
      int markX = markCursorX(anchor, lastBase, scaledMark);
      int markY = yPos - markRaise(anchor, scaledMark, lastBase);
      renderScaledChar(*resolvedFont, cp, &markX, &markY, black, style, kSmallCapsScalePct);
      continue;
    }

    const int baseX = xpos;
    renderScaledChar(*resolvedFont, cp, &xpos, &yCursor, black, style, kSmallCapsScalePct);
    if (lookupGlyph(gfx, *resolvedFont, cp, style, glyphStorage, glyph, true)) {
      lastBase = {baseX,
                  (glyph->left * static_cast<int>(kSmallCapsScalePct) + kScaleRoundBias) / 100,
                  std::max(1, (static_cast<int>(glyph->width) * kSmallCapsScalePct + kScaleRoundBias) / 100),
                  std::max(1, (glyph->top * static_cast<int>(kSmallCapsScalePct) + kScaleRoundBias) / 100), true};
    } else {
      lastBase.valid = false;
    }
  }
  return xpos;
}

void TextRender::centered(const int fontId, const int y, const char* text, const bool black,
                          const EpdFontFamily::Style style) const {
  const int x = (gfx.getScreenWidth() - getWidth(fontId, text, style)) / 2;
  render(fontId, x, y, text, black, style);
}

void TextRender::renderChar(const EpdFontFamily& fontFamily, const uint32_t cp, int* x, const int* y,
                            const bool pixelState, const EpdFontFamily::Style style, const bool gray) const {
  EpdGlyph glyphStorage{};
  const EpdGlyph* glyph = nullptr;
  const EpdFontData* fontData = fontFamily.getData(style);
  if (!fontData) {
    return;
  }
  ExternalFont* it = findStreamingFont(gfx, fontData);
  if (it) {
    if (it->getGlyphMetadata(cp, glyphStorage) || it->getGlyphMetadata(REPLACEMENT_GLYPH, glyphStorage)) {
      glyph = &glyphStorage;
    }
  } else {
    glyph = fontFamily.getGlyph(cp, style);
  }

  if (!glyph) {
    glyph = fontFamily.getGlyph(REPLACEMENT_GLYPH, style);
  }

  if (!glyph) {
    return;
  }

  if (fontData->bitmap != nullptr && !embeddedGlyphBitmapIsValid(fontData, glyph)) {
    const EpdGlyph* replacement = fontFamily.getGlyph(REPLACEMENT_GLYPH, style);
    if (embeddedGlyphBitmapIsValid(fontData, replacement)) {
      glyph = replacement;
    } else {
      *x += glyphAdvanceX(*glyph);
      return;
    }
  }

  const int is2Bit = fontData->is2Bit;
  const uint8_t width = glyph->width;
  const uint8_t height = glyph->height;
  const int left = glyph->left;

  const uint8_t* bitmap = nullptr;
  uint8_t localStackBuffer[2048];

  if (fontData->bitmap != nullptr) {
    if (!embeddedGlyphBitmapIsValid(fontData, glyph)) {
      INX_SERIAL.printf("[%lu] [GFX][FONT] invalid embedded glyph cp=U+%04lX style=%d width=%u height=%u "
                        "offset=%lu length=%u bitmapSize=%lu\n",
                        millis(), static_cast<unsigned long>(cp), static_cast<int>(style), glyph->width, glyph->height,
                        static_cast<unsigned long>(glyph->dataOffset), glyph->dataLength,
                        static_cast<unsigned long>(fontData->bitmapSize));
      glyph = fontFamily.getGlyph(REPLACEMENT_GLYPH, style);
      if (!embeddedGlyphBitmapIsValid(fontData, glyph)) {
        INX_SERIAL.printf("[%lu] [GFX][FONT] replacement glyph invalid cp=U+%04lX style=%d\n", millis(),
                          static_cast<unsigned long>(cp), static_cast<int>(style));
        *x += 1;
        return;
      }
    }
    bitmap = &fontData->bitmap[glyph->dataOffset];
  } else if (it) {
    const size_t dataLen = glyph->dataLength;
    if (dataLen <= sizeof(localStackBuffer)) {
      if (it->getGlyphBitmap(glyph->dataOffset, dataLen, localStackBuffer)) {
        bitmap = localStackBuffer;
      } else {
        *x += glyphAdvanceX(*glyph);
        return;
      }
    } else {
      constexpr size_t kMaxRowBytes = 512;
      const size_t rowBytes = is2Bit ? (static_cast<size_t>(width) + 3u) / 4u : (static_cast<size_t>(width) + 7u) / 8u;
      if (rowBytes == 0 || rowBytes > kMaxRowBytes) {
        *x += glyphAdvanceX(*glyph);
        return;
      }
      uint8_t rowBuf[kMaxRowBytes];
      for (int glyphY = 0; glyphY < height; glyphY++) {
        const size_t rowPixelOffset = is2Bit ? (static_cast<size_t>(glyphY) * width) % 4u : 0u;
        const size_t rowStartByte = is2Bit ? (static_cast<size_t>(glyphY) * width) / 4u
                                           : static_cast<size_t>(glyphY) * rowBytes;
        const size_t packedRowBytes = is2Bit ? (rowPixelOffset + width + 3u) / 4u : rowBytes;
        const uint32_t rowOff = glyph->dataOffset + static_cast<uint32_t>(rowStartByte);
        if (!it->getGlyphBitmap(rowOff, packedRowBytes, rowBuf)) {
          *x += glyphAdvanceX(*glyph);
          return;
        }
        const int screenY = *y - glyph->top + glyphY;
        for (int glyphX = 0; glyphX < width; glyphX++) {
          const int screenX = *x + left + glyphX;
          if (is2Bit) {
            const uint8_t bmpVal = 3 - read2BitRowPixel(rowBuf, rowPixelOffset, glyphX);

            if (gray && gfx.renderMode == GfxRenderer::BW && bmpVal < 3) {
              if (((screenX + screenY) & 1) == 0) {
                gfx.drawPixel(screenX, screenY, pixelState);
              }
            } else if (!pixelState && bmpVal < 3) {
              renderSolidTextPixel(gfx, screenX, screenY, false);
            } else if (gfx.renderMode == GfxRenderer::BW && bmpVal < 3) {
              gfx.drawPixel(screenX, screenY, pixelState);
            } else {
              renderMediumTextPlanePixel(gfx, screenX, screenY, imageLevelFromFontBmpVal(bmpVal));
            }
          } else {
            const bool ink = read1BitRowPixel(rowBuf, width, glyphX);
            if (ink && (!gray || ((screenX + screenY) & 1) == 0)) {
              renderSolidTextPixel(gfx, screenX, screenY, pixelState);
            }
          }
        }
      }
      *x += glyphAdvanceX(*glyph);
      return;
    }
  }

  if (bitmap != nullptr) {
    for (int glyphY = 0; glyphY < height; glyphY++) {
      const int screenY = *y - glyph->top + glyphY;
      for (int glyphX = 0; glyphX < width; glyphX++) {
        const int pixelPosition = glyphY * width + glyphX;
        const int screenX = *x + left + glyphX;

        if (is2Bit) {
          const uint8_t byte = bitmap[pixelPosition / 4];
          const uint8_t bitIndex = (3 - (pixelPosition % 4)) * 2;
          const uint8_t bmpVal = 3 - ((byte >> bitIndex) & 0x3);

          if (gray && gfx.renderMode == GfxRenderer::BW && bmpVal < 3) {
            if (((screenX + screenY) & 1) == 0) {
              gfx.drawPixel(screenX, screenY, pixelState);
            }
          } else if (!pixelState && bmpVal < 3) {
            renderSolidTextPixel(gfx, screenX, screenY, false);
          } else if (gfx.renderMode == GfxRenderer::BW && bmpVal < 3) {
            gfx.drawPixel(screenX, screenY, pixelState);
          } else {
            renderMediumTextPlanePixel(gfx, screenX, screenY, imageLevelFromFontBmpVal(bmpVal));
          }
        } else {
          const bool ink = read1BitGlyphPixel(bitmap, width, height, glyphX, glyphY);
          if (ink && (!gray || ((screenX + screenY) & 1) == 0)) {
            renderSolidTextPixel(gfx, screenX, screenY, pixelState);
          }
        }
      }
    }
  }

  *x += glyphAdvanceX(*glyph);
}

void TextRender::renderScaledChar(const EpdFontFamily& fontFamily, const uint32_t cp, int* x, const int* y,
                                  const bool pixelState, const EpdFontFamily::Style style,
                                  const uint8_t scalePct, const bool gray) const {
  EpdGlyph glyphStorage{};
  const EpdGlyph* glyph = nullptr;
  const EpdFontData* fontData = fontFamily.getData(style);
  if (!fontData) {
    return;
  }
  ExternalFont* it = findStreamingFont(gfx, fontData);
  if (it) {
    if (it->getGlyphMetadata(cp, glyphStorage) || it->getGlyphMetadata(REPLACEMENT_GLYPH, glyphStorage)) {
      glyph = &glyphStorage;
    }
  } else {
    glyph = fontFamily.getGlyph(cp, style);
  }

  if (!glyph) {
    glyph = fontFamily.getGlyph(REPLACEMENT_GLYPH, style);
  }
  if (!glyph) {
    return;
  }

  if (fontData->bitmap != nullptr && !embeddedGlyphBitmapIsValid(fontData, glyph)) {
    const EpdGlyph* replacement = fontFamily.getGlyph(REPLACEMENT_GLYPH, style);
    if (embeddedGlyphBitmapIsValid(fontData, replacement)) {
      glyph = replacement;
    } else {
      *x += glyphAdvanceX(*glyph);
      return;
    }
  }

  const int is2Bit = fontData->is2Bit;
  const uint8_t width = glyph->width;
  const uint8_t height = glyph->height;
  const int scaledAdvanceX = scaleMetricRound(glyphAdvanceX(*glyph), scalePct);
  const int scaledLeft = (glyph->left * static_cast<int>(scalePct) + kScaleRoundBias) / 100;
  const int scaledTop = std::max(1, (glyph->top * static_cast<int>(scalePct) + kScaleRoundBias) / 100);
  const int scaledW = std::max(1, (static_cast<int>(width) * static_cast<int>(scalePct) + kScaleRoundBias) / 100);
  const int scaledH = std::max(1, (static_cast<int>(height) * static_cast<int>(scalePct) + kScaleRoundBias) / 100);

  const uint8_t* bitmap = nullptr;
  uint8_t localStackBuffer[2048];
  if (fontData->bitmap != nullptr) {
    bitmap = &fontData->bitmap[glyph->dataOffset];
  } else if (it) {
    const size_t dataLen = glyph->dataLength;
    if (dataLen <= sizeof(localStackBuffer)) {
      if (it->getGlyphBitmap(glyph->dataOffset, dataLen, localStackBuffer)) {
        bitmap = localStackBuffer;
      } else {
        *x += scaledAdvanceX;
        return;
      }
    } else {
      constexpr size_t kMaxRowBytes = 512;
      const size_t rowBytes = is2Bit ? (static_cast<size_t>(width) + 3u) / 4u : (static_cast<size_t>(width) + 7u) / 8u;
      if (rowBytes == 0 || rowBytes > kMaxRowBytes) {
        *x += scaledAdvanceX;
        return;
      }
      uint8_t rowBuf[kMaxRowBytes];
      for (int outY = 0; outY < scaledH; ++outY) {
        const int srcY = std::min<int>(height - 1, (outY * static_cast<int>(height)) / scaledH);
        const size_t rowPixelOffset = is2Bit ? (static_cast<size_t>(srcY) * width) % 4u : 0u;
        const size_t rowStartByte = is2Bit ? (static_cast<size_t>(srcY) * width) / 4u
                                           : static_cast<size_t>(srcY) * rowBytes;
        const size_t packedRowBytes = is2Bit ? (rowPixelOffset + width + 3u) / 4u : rowBytes;
        const uint32_t rowOff = glyph->dataOffset + static_cast<uint32_t>(rowStartByte);
        if (!it->getGlyphBitmap(rowOff, packedRowBytes, rowBuf)) {
          *x += scaledAdvanceX;
          return;
        }
        const int screenY = *y - scaledTop + outY;
        for (int outX = 0; outX < scaledW; ++outX) {
          int sx0 = (outX * static_cast<int>(width)) / scaledW;
          int sx1 = ((outX + 1) * static_cast<int>(width)) / scaledW;
          if (sx1 <= sx0) sx1 = sx0 + 1;
          sx1 = std::min<int>(sx1, width);
          const int screenX = *x + scaledLeft + outX;
          if (is2Bit) {
            uint16_t rawSum = 0;
            int sampleCount = 0;
            for (int sx = sx0; sx < sx1; ++sx) {
              const uint8_t raw = read2BitRowPixel(rowBuf, rowPixelOffset, sx);
              rawSum += raw;
              ++sampleCount;
            }
            const uint8_t rawAverage = static_cast<uint8_t>((rawSum + sampleCount / 2) / sampleCount);
            const uint8_t bmpVal = 3 - rawAverage;
            if (gray && gfx.renderMode == GfxRenderer::BW && bmpVal < 3) {
              if (((screenX + screenY) & 1) == 0) {
                gfx.drawPixel(screenX, screenY, pixelState);
              }
            } else if (!pixelState && bmpVal < 3) {
              renderSolidTextPixel(gfx, screenX, screenY, false);
            } else if (gfx.renderMode == GfxRenderer::BW && bmpVal < 3) {
              gfx.drawPixel(screenX, screenY, pixelState);
            } else {
              renderMediumTextPlanePixel(gfx, screenX, screenY, imageLevelFromFontBmpVal(bmpVal));
            }
          } else {
            int inkCount = 0;
            int sampleCount = 0;
            for (int sx = sx0; sx < sx1; ++sx) {
              if ((rowBuf[sx / 8] >> (7 - (sx % 8))) & 1) {
                ++inkCount;
              }
              ++sampleCount;
            }
            const bool ink = inkCount * 2 >= sampleCount;
            if (ink && (!gray || ((screenX + screenY) & 1) == 0)) {
              renderSolidTextPixel(gfx, screenX, screenY, pixelState);
            }
          }
        }
      }
      *x += scaledAdvanceX;
      return;
    }
  }

  if (bitmap != nullptr) {
    for (int outY = 0; outY < scaledH; ++outY) {
      int sy0 = (outY * static_cast<int>(height)) / scaledH;
      int sy1 = ((outY + 1) * static_cast<int>(height)) / scaledH;
      if (sy1 <= sy0) sy1 = sy0 + 1;
      sy1 = std::min<int>(sy1, height);
      const int screenY = *y - scaledTop + outY;
      for (int outX = 0; outX < scaledW; ++outX) {
        int sx0 = (outX * static_cast<int>(width)) / scaledW;
        int sx1 = ((outX + 1) * static_cast<int>(width)) / scaledW;
        if (sx1 <= sx0) sx1 = sx0 + 1;
        sx1 = std::min<int>(sx1, width);
        const int screenX = *x + scaledLeft + outX;
        if (is2Bit) {
          uint16_t rawSum = 0;
          int sampleCount = 0;
          for (int sy = sy0; sy < sy1; ++sy) {
            for (int sx = sx0; sx < sx1; ++sx) {
              const int pp = sy * width + sx;
              const uint8_t raw = (bitmap[pp / 4] >> ((3 - (pp % 4)) * 2)) & 0x3;
              rawSum += raw;
              ++sampleCount;
            }
          }
          const uint8_t rawAverage = static_cast<uint8_t>((rawSum + sampleCount / 2) / sampleCount);
          const uint8_t bmpVal = 3 - rawAverage;
          if (gray && gfx.renderMode == GfxRenderer::BW && bmpVal < 3) {
            if (((screenX + screenY) & 1) == 0) {
              gfx.drawPixel(screenX, screenY, pixelState);
            }
          } else if (!pixelState && bmpVal < 3) {
            renderSolidTextPixel(gfx, screenX, screenY, false);
          } else if (gfx.renderMode == GfxRenderer::BW && bmpVal < 3) {
            gfx.drawPixel(screenX, screenY, pixelState);
          } else {
            renderMediumTextPlanePixel(gfx, screenX, screenY, imageLevelFromFontBmpVal(bmpVal));
          }
        } else {
          int inkCount = 0;
          int sampleCount = 0;
          for (int sy = sy0; sy < sy1; ++sy) {
            for (int sx = sx0; sx < sx1; ++sx) {
              const int pp = sy * width + sx;
              if ((bitmap[pp / 8] >> (7 - (pp % 8))) & 1) {
                ++inkCount;
              }
              ++sampleCount;
            }
          }
          const bool ink = inkCount * 2 >= sampleCount;
          if (ink && (!gray || ((screenX + screenY) & 1) == 0)) {
            renderSolidTextPixel(gfx, screenX, screenY, pixelState);
          }
        }
      }
    }
  }
  *x += scaledAdvanceX;
}

int TextRender::getStreamingTextWidth(const EpdFontFamily& family, const char* text,
                                      const EpdFontFamily::Style style) const {
  const EpdFontData* data = family.getData(style);
  ExternalFont* it = findStreamingFont(gfx, data);
  if (!it) {
    return 0;
  }

  int totalWidth = 0;
  uint32_t cp;
  const uint8_t* ptr = reinterpret_cast<const uint8_t*>(text);

  while ((cp = utf8NextCodepoint(&ptr))) {
    EpdGlyph glyph;
    if (!it->getGlyphMetadata(cp, glyph)) {
      if (!it->getGlyphMetadata(REPLACEMENT_GLYPH, glyph)) {
        continue;
      }
    }
    totalWidth += glyphAdvanceX(glyph);
  }
  return totalWidth;
}
