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
  int sourceFontId;  // the built-in size tier this word's original PDF font size mapped to - not for
                     // rendering (the reader's own chosen font/size is used for that), only so
                     // extractStyledParagraphs() can flag paragraphs that were visually larger than the
                     // document's body text as headings, by comparing against the page's most common tier.
  int16_t sourceX = 0;      // source PDF x position, in points; used to retain list/first-line indentation
  bool lineBreakBefore = false;  // source PDF placed this word on a new line
};

struct PdfParagraph {
  std::vector<PdfStyledWord> words;
  bool heading = false;

  // Set instead of `words` for a standalone image block (JPEG-filtered images only - see extractStyledParagraphs).
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

  // Concatenates every page's text in approximate reading order into one reflowable string, inferring line
  // wraps vs. paragraph/section breaks from the vertical gap between consecutive text runs (small gap: same
  // line; ~one line height: wrapped continuation, joined with a space; larger: paragraph break, "\n\n").
  // This heuristic isn't perfect PDF structure recovery, but it's what lets PDFs be read like any other
  // reflowable book - via the existing, proven Txt/TxtReaderActivity pipeline - instead of a fixed-layout
  // page-image view (see PdfReaderActivity's removal in favor of this).
  std::string extractPlainText() const;

  // Like extractPlainText(), but preserves per-word style (bold/italic, from the PDF's own font selection)
  // instead of flattening to plain text, flags paragraphs whose text was notably larger than the document's
  // body size as headings, and emits JPEG-filtered images as their own paragraph entries interleaved in
  // document order (raw/FlateDecode-sample images are skipped - out of scope for this pass). This is what
  // PdfReaderActivity lays out into cached Page/TextBlock/PageImage objects, so headings/emphasis/images
  // actually render instead of being discarded.
  //
  // [startPage, endPage) scopes extraction to a page range so PdfReaderActivity can build the book in
  // chunks instead of parsing the whole thing up front - since a paragraph never spans a page boundary
  // (each source page always ends its last paragraph, see the emplace_back() after the per-page render
  // loop), a range is a self-contained unit with no cross-chunk continuation to worry about. The one
  // heuristic that isn't chunk-independent is heading detection, which compares each paragraph's font size
  // against the *body* size - that's a majority vote over whatever paragraphs are in the requested range,
  // so it's only really representative when the range is a reasonable fraction of the book, not one page.
  std::vector<PdfParagraph> extractStyledParagraphs(int startPage, int endPage) const;

  // Scale factor (points -> device pixels) that fits page `index` within targetWidthPx x targetHeightPx
  // without overflowing either dimension - whichever dimension is more constraining wins, so a page whose
  // aspect ratio doesn't match the screen's usable area gets letterboxed rather than cropped.
  double getPageScale(int index, int targetWidthPx, int targetHeightPx) const;

  // Height, in device pixels, of page `index` when rendered at the given scale (see getPageScale()).
  int getPageHeightPx(int index, double scale) const;

  // Interprets page `index`'s content stream at the given scale (see getPageScale()) and invokes onTextRun
  // once per text-showing operation, with positions already scaled to device pixels (top-left origin).
  // Returns false on a missing/corrupt page. `outStats`, if non-null, is filled in so callers can tell a
  // legitimately blank page apart from one whose font(s) couldn't be handled at all (rare: non-Identity
  // Type0 CMaps, Type3).
  bool renderPage(int index, double scale, const std::function<void(const PdfTextRun&)>& onTextRun,
                  PdfRenderStats* outStats = nullptr,
                  const std::function<void(const PdfDrawCommand&)>& onDraw = {},
                  const std::function<int(int fontId, const char* utf8Text, EpdFontFamily::Style style)>&
                      measureTextWidthPx = {}) const;

  // Convenience used by both the live reader activity and cover/thumbnail generation: computes the fit-to-box
  // scale, runs renderPage(), and draws each run directly via renderer.text.render() (measuring each run's
  // actual rendered width to keep cursor advancement in sync with what's really drawn - see
  // PdfContentInterpreter's measureTextWidthPx) at (offsetX + run.x, offsetY + run.y).
  bool renderPageToRenderer(int index, GfxRenderer& renderer, int offsetX, int offsetY, int targetWidthPx,
                            int targetHeightPx, PdfRenderStats* outStats = nullptr) const;

  std::string getCoverBmpPath() const;
  bool generateCoverBmp(GfxRenderer& renderer) const;

  std::string getThumbBmpPath() const;
  bool generateThumbBmp(GfxRenderer& renderer) const;
};
