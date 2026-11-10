#pragma once

/**
 * @file EpdFontFamily.h
 * @brief Public interface and types for EpdFontFamily.
 */

#include "EpdFont.h"

class EpdFontFamily {
 public:
  enum Style : uint8_t { REGULAR = 0, BOLD = 1, ITALIC = 2, BOLD_ITALIC = 3 };

  explicit EpdFontFamily(const EpdFont* regular, const EpdFont* bold = nullptr, const EpdFont* italic = nullptr,
                         const EpdFont* boldItalic = nullptr)
      : regular(regular), bold(bold), italic(italic), boldItalic(boldItalic) {}
  ~EpdFontFamily() = default;
  void getTextDimensions(const char* string, int* w, int* h, Style style = REGULAR) const;
  bool hasPrintableChars(const char* string, Style style = REGULAR) const;
  const EpdFontData* getData(Style style = REGULAR) const;
  /** Inspect resident data without loading a style (used while removing fonts). */
  const EpdFontData* getLoadedData(Style style = REGULAR) const;
  void setStyleLoader(void (*loader)(void*, Style), void* context) {
    styleLoader = loader;
    styleLoaderContext = context;
  }
  const EpdGlyph* getGlyph(uint32_t cp, Style style = REGULAR) const;
  void setData(Style style, const EpdFontData* data);

 private:
  const EpdFont* regular;
  const EpdFont* bold;
  const EpdFont* italic;
  const EpdFont* boldItalic;

  void (*styleLoader)(void*, Style) = nullptr;
  void* styleLoaderContext = nullptr;
  const EpdFont* getFont(Style style, bool loadStyle = true) const;
};
