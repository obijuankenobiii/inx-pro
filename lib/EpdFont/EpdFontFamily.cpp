/**
 * @file EpdFontFamily.cpp
 * @brief Definitions for EpdFontFamily.
 */

#include "EpdFontFamily.h"

void EpdFontFamily::setData(Style style, const EpdFontData* data) {
  EpdFont* target = const_cast<EpdFont*>(getFont(style));

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
const EpdFont* EpdFontFamily::getFont(const Style style) const {
  if (style == BOLD && bold) return bold;
  if (style == ITALIC && italic) return italic;
  if (style == BOLD_ITALIC) {
    if (boldItalic) return boldItalic;
    if (bold) return bold;
    if (italic) return italic;
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

const EpdGlyph* EpdFontFamily::getGlyph(const uint32_t cp, const Style style) const {
  const EpdFont* font = getFont(style);
  if (!font || !font->data || !font->data->glyph) return nullptr;

  return font->getGlyph(cp);
}