/**
 * @file Pdf.h
 * @brief Public interface and types for Pdf.
 */

/**
 * Pdf.h
 *
 * Embedded PDF ebook handler for Inx Reader. It renders text and common vector paths using the firmware's
 * built-in fonts and framebuffer primitives; see PdfDocument.h and PdfFont.h for the supported PDF subset.
 */

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "Pdf/PdfContentInterpreter.h"
#include "Pdf/PdfDocument.h"

class GfxRenderer;

struct PdfStyledWord {
  std::string text;
  EpdFontFamily::Style style;
  int sourceFontId;
  int16_t sourceX = 0;
  bool lineBreakBefore = false;
};

struct PdfParagraph {
  std::vector<PdfStyledWord> words;
  bool heading = false;

  std::vector<uint8_t> imageJpegData;
  int imageIntrinsicWidth = 0;
  int imageIntrinsicHeight = 0;
  bool isImage() const { return !imageJpegData.empty(); }
};

class Pdf {
  std::string filepath;
  std::string cachePath;
  mutable PdfDocument doc;
  bool loaded;

 public:
  explicit Pdf(std::string filepath, const std::string& cacheDir) : filepath(std::move(filepath)), loaded(false) {
    cachePath = cacheDir + "/" + std::to_string(std::hash<std::string>{}(this->filepath));
  }
  ~Pdf() = default;

  bool load();
  bool clearCache() const;
  void setupCacheDir() const;

  const std::string& getCachePath() const { return cachePath; }
  const std::string& getPath() const { return filepath; }

  std::string getTitle() const;
  std::string getAuthor() const { return ""; }

  bool isLoaded() const { return loaded; }
  int getPageCount() const;

  std::string extractPlainText() const;

  std::vector<PdfParagraph> extractStyledParagraphs(int startPage, int endPage) const;

  double getPageScale(int index, int targetWidthPx, int targetHeightPx) const;

  int getPageHeightPx(int index, double scale) const;

  bool renderPage(int index, double scale, const std::function<void(const PdfTextRun&)>& onTextRun,
                  PdfRenderStats* outStats = nullptr,
                  const std::function<void(const PdfDrawCommand&)>& onDraw = {},
                  const std::function<int(int fontId, const char* utf8Text, EpdFontFamily::Style style)>&
                      measureTextWidthPx = {}) const;

  bool renderPageToRenderer(int index, GfxRenderer& renderer, int offsetX, int offsetY, int targetWidthPx,
                            int targetHeightPx, PdfRenderStats* outStats = nullptr) const;

  std::string getCoverBmpPath() const;
  bool generateCoverBmp(GfxRenderer& renderer) const;

  std::string getThumbBmpPath() const;
  bool generateThumbBmp(GfxRenderer& renderer) const;
};
