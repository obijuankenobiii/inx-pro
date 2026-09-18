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
  text = RtlText::prepareForRender(text, visualText);
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
    bool needsFallback = false;
    const uint8_t* streamPtr = reinterpret_cast<const uint8_t*>(text);
    while (const uint32_t cp = utf8NextCodepoint(&streamPtr)) {
      EpdGlyph glyph{};
      if (stream->getGlyphMetadata(cp, glyph)) {
        streamingWidth += glyph.advanceX;
        continue;
      }

      const int resolved = resolveFontForCodepoint(gfx, fontId, cp, style);
      if (resolved != fontId) {
        needsFallback = true;
        break;
      }
      if (stream->getGlyphMetadata(REPLACEMENT_GLYPH, glyph)) {
        streamingWidth += glyph.advanceX;
      }
    }
    if (!needsFallback) {
      return streamingWidth;
    }
  }

  // Keep the original family measurement for the common path. Besides being
  // cheaper for embedded fonts, this preserves their existing bearing rules.
  bool hasFallbackGlyph = false;
  const uint8_t* scan = reinterpret_cast<const uint8_t*>(text);
  while (const uint32_t cp = utf8NextCodepoint(&scan)) {
    if (!hasGlyph(gfx, *primary, cp, style)) {
      const int resolved = resolveFontForCodepoint(gfx, fontId, cp, style);
      if (resolved != fontId) {
        hasFallbackGlyph = true;
        break;
      }
    }
  }
  if (!hasFallbackGlyph) {
    const auto& family = *primary;
    if (findStreamingFont(gfx, family.getData(style)) != nullptr) {
      return getStreamingTextWidth(family, text, style);
    }
    int w = 0;
    int h = 0;
    family.getTextDimensions(text, &w, &h, style);
    return w;
  }

  int totalWidth = 0;
  const uint8_t* ptr = reinterpret_cast<const uint8_t*>(text);
  while (const uint32_t cp = utf8NextCodepoint(&ptr)) {
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
    if (glyph) totalWidth += glyph->advanceX;
  }
  return totalWidth;
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
  return glyph ? glyph->advanceX : 0;
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
  text = RtlText::prepareForRender(text, visualText);
  if (!text || *text == '\0' || findFontFamily(gfx, fontId) == nullptr) {
    return 0;
  }

  const std::string upper = toUpperUtf8(text);
  const char* ptr = upper.c_str();
  int totalWidth = 0;
  while (const uint32_t cp = utf8NextCodepoint(reinterpret_cast<const uint8_t**>(&ptr))) {
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
    totalWidth += scaleMetricRound(glyph->advanceX, kSmallCapsScalePct);
  }
  return totalWidth;
}

int TextRender::getScaledWidth(const int fontId, const char* text, const uint8_t scalePct,
                               const EpdFontFamily::Style style) const {
  text = LanguageManager::translateText(text);
  std::string visualText;
  text = RtlText::prepareForRender(text, visualText);
  if (!text || *text == '\0' || findFontFamily(gfx, fontId) == nullptr) {
    return 0;
  }

  const uint8_t* ptr = reinterpret_cast<const uint8_t*>(text);
  int totalWidth = 0;
  while (const uint32_t cp = utf8NextCodepoint(&ptr)) {
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
    totalWidth += scaleMetricRound(glyph->advanceX, scalePct);
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
  text = RtlText::prepareForRender(text, visualText);
  if (text == nullptr || *text == '\0' || findFontFamily(gfx, fontId) == nullptr) {
    return;
  }
  int yPos = y;

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
    yPos -= glyph->advanceX;
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
  text = RtlText::prepareForRender(text, visualText);
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
  while ((cp = utf8NextCodepoint(reinterpret_cast<const uint8_t**>(&text)))) {
    const int resolvedId = resolveFontForCodepoint(gfx, fontId, cp, style);
    const EpdFontFamily* resolvedFont = findFontFamily(gfx, resolvedId);
    if (resolvedFont) renderChar(*resolvedFont, cp, &xpos, &yCursor, black, style, false);
  }
}

void TextRender::renderGray(const int fontId, const int x, const int y, const char* text, const bool black,
                            const EpdFontFamily::Style style) const {
  text = LanguageManager::translateText(text);
  std::string visualText;
  text = RtlText::prepareForRender(text, visualText);
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
  while ((cp = utf8NextCodepoint(reinterpret_cast<const uint8_t**>(&text)))) {
    const int resolvedId = resolveFontForCodepoint(gfx, fontId, cp, style);
    const EpdFontFamily* resolvedFont = findFontFamily(gfx, resolvedId);
    if (resolvedFont) renderChar(*resolvedFont, cp, &xpos, &yCursor, black, style, true);
  }
}

int TextRender::renderScaled(const int fontId, const int x, const int y, const char* text, const uint8_t scalePct,
                             const bool black, const EpdFontFamily::Style style) const {
  text = LanguageManager::translateText(text);
  std::string visualText;
  text = RtlText::prepareForRender(text, visualText);
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

  uint32_t cp;
  while ((cp = utf8NextCodepoint(reinterpret_cast<const uint8_t**>(&text)))) {
    const int resolvedId = resolveFontForCodepoint(gfx, fontId, cp, style);
    const EpdFontFamily* resolvedFont = findFontFamily(gfx, resolvedId);
    if (resolvedFont) renderScaledChar(*resolvedFont, cp, &xpos, &yCursor, black, style, scalePct);
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
  const char* ptr = RtlText::prepareForRender(upper.c_str(), visualText);
  const int yPos = y + getFontAscenderSize(fontId);
  int xpos = x;
  int yCursor = yPos;

  uint32_t cp;
  while ((cp = utf8NextCodepoint(reinterpret_cast<const uint8_t**>(&ptr)))) {
    const int resolvedId = resolveFontForCodepoint(gfx, fontId, cp, style);
    const EpdFontFamily* resolvedFont = findFontFamily(gfx, resolvedId);
    if (resolvedFont) renderScaledChar(*resolvedFont, cp, &xpos, &yCursor, black, style, kSmallCapsScalePct);
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
        *x += glyph->advanceX;
        return;
      }
    } else {
      constexpr size_t kMaxRowBytes = 512;
      const size_t rowBytes = is2Bit ? (static_cast<size_t>(width) + 3u) / 4u : (static_cast<size_t>(width) + 7u) / 8u;
      if (rowBytes == 0 || rowBytes > kMaxRowBytes) {
        *x += glyph->advanceX;
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
          *x += glyph->advanceX;
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
      *x += glyph->advanceX;
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

  *x += glyph->advanceX;
}

void TextRender::renderScaledChar(const EpdFontFamily& fontFamily, const uint32_t cp, int* x, const int* y,
                                  const bool pixelState, const EpdFontFamily::Style style,
                                  const uint8_t scalePct) const {
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

  const int is2Bit = fontData->is2Bit;
  const uint8_t width = glyph->width;
  const uint8_t height = glyph->height;
  const int scaledAdvanceX = scaleMetricRound(glyph->advanceX, scalePct);
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
            uint8_t rawMax = 0;
            for (int sx = sx0; sx < sx1; ++sx) {
              const uint8_t raw = read2BitRowPixel(rowBuf, rowPixelOffset, sx);
              if (raw > rawMax) rawMax = raw;
            }
            const uint8_t bmpVal = 3 - rawMax;
            if (!pixelState && bmpVal < 3) {
              renderSolidTextPixel(gfx, screenX, screenY, false);
            } else if (gfx.renderMode == GfxRenderer::BW && bmpVal < 3) {
              gfx.drawPixel(screenX, screenY, pixelState);
            } else {
              renderMediumTextPlanePixel(gfx, screenX, screenY, imageLevelFromFontBmpVal(bmpVal));
            }
          } else {
            bool ink = false;
            for (int sx = sx0; sx < sx1; ++sx) {
              if ((rowBuf[sx / 8] >> (7 - (sx % 8))) & 1) {
                ink = true;
                break;
              }
            }
            if (ink) {
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
          uint8_t rawMax = 0;
          for (int sy = sy0; sy < sy1; ++sy) {
            for (int sx = sx0; sx < sx1; ++sx) {
              const int pp = sy * width + sx;
              const uint8_t raw = (bitmap[pp / 4] >> ((3 - (pp % 4)) * 2)) & 0x3;
              if (raw > rawMax) rawMax = raw;
            }
          }
          const uint8_t bmpVal = 3 - rawMax;
          if (!pixelState && bmpVal < 3) {
            renderSolidTextPixel(gfx, screenX, screenY, false);
          } else if (gfx.renderMode == GfxRenderer::BW && bmpVal < 3) {
            gfx.drawPixel(screenX, screenY, pixelState);
          } else {
            renderMediumTextPlanePixel(gfx, screenX, screenY, imageLevelFromFontBmpVal(bmpVal));
          }
        } else {
          bool ink = false;
          for (int sy = sy0; sy < sy1 && !ink; ++sy) {
            for (int sx = sx0; sx < sx1; ++sx) {
              const int pp = sy * width + sx;
              if ((bitmap[pp / 8] >> (7 - (pp % 8))) & 1) {
                ink = true;
                break;
              }
            }
          }
          if (ink) {
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
    totalWidth += glyph.advanceX;
  }
  return totalWidth;
}
