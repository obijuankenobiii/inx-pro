/**
 * @file EpdFontFamily.cpp
 * @brief Definitions for EpdFontFamily.
 */

#include "EpdFontFamily.h"

void EpdFontFamily::setData(Style style, const EpdFontData* data) {
  // Select the exact style: getFont() falls back to regular and would replace
  // the regular face when attaching a not-yet-loaded bold/italic style.
  EpdFont* target = const_cast<EpdFont*>(style == BOLD ? bold : style == ITALIC ? italic :
                                       style == BOLD_ITALIC ? boldItalic : regular);

  if (!target) {
    target = new EpdFont(data);

    if (style == BOLD)
      bold = target;
    else if (style == ITALIC)
      italic = target;
    else if (style == BOLD_ITALIC)
      boldItalic = target;
    else
      regular = target;
  } else {
    target->data = const_cast<EpdFontData*>(data);
  }
}
const EpdFont* EpdFontFamily::getFont(const Style style, const bool loadStyle) const {
  const EpdFont* requested = style == BOLD ? bold : style == ITALIC ? italic :
                             style == BOLD_ITALIC ? boldItalic : regular;
  if (style != REGULAR && (!requested || !requested->data) && loadStyle && styleLoader) {
    styleLoader(styleLoaderContext, style);
  }
  if (style == BOLD && bold && bold->data) return bold;
  if (style == ITALIC && italic && italic->data) return italic;
  if (style == BOLD_ITALIC) {
    if (boldItalic && boldItalic->data) return boldItalic;
    const EpdFont* fallback = getFont(BOLD, loadStyle);
    if (fallback != regular) return fallback;
    fallback = getFont(ITALIC, loadStyle);
    if (fallback != regular) return fallback;
  }
  return regular;
}

void EpdFontFamily::getTextDimensions(const char* string, int* w, int* h, const Style style) const {
  const EpdFont* font = getFont(style);
  if (font && font->data && font->data->glyph) {
    font->getTextDimensions(string, w, h);
  } else {
    if (w) *w = 0;
    if (h) *h = 0;
  }
}

bool EpdFontFamily::hasPrintableChars(const char* string, const Style style) const {
  const EpdFont* font = getFont(style);
  if (!font || !font->data) return false;

  if (font->data->glyph == nullptr) return true;

  return font->hasPrintableChars(string);
}

const EpdFontData* EpdFontFamily::getData(const Style style) const {
  const EpdFont* font = getFont(style);
  return font ? font->data : nullptr;
}

const EpdFontData* EpdFontFamily::getLoadedData(const Style style) const {
  const EpdFont* font = getFont(style, false);
  return font ? font->data : nullptr;
}

const EpdGlyph* EpdFontFamily::getGlyph(const uint32_t cp, const Style style) const {
  const EpdFont* font = getFont(style);
  if (!font || !font->data || !font->data->glyph) return nullptr;

  return font->getGlyph(cp);
}
