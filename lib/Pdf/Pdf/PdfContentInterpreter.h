/**
 * @file PdfContentInterpreter.h
 * @brief Content-stream interpreter for text and common vector paths. Images and advanced transparency/shading
 * remain outside the embedded renderer's current scope.
 */

#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

#include <EpdFontFamily.h>

#include "PdfFont.h"
#include "PdfObject.h"

class PdfDocument;
class PdfLexer;

struct PdfTextRun {
  double x = 0;  // device pixels, top-left origin, x-right
  double y = 0;  // device pixels, top-left origin, y-down (already flipped from PDF's bottom-up space)
  double sourceFontSizePts = 0;  // source PDF font size in points, before the caller's page scale
  int fontId = 0;
  EpdFontFamily::Style style = EpdFontFamily::REGULAR;
  std::string utf8Text;
};

// Lets callers tell "legitimately blank page" apart from "page has text, but Phase 1 can't render its
// font(s)" - both currently look identical (zero PdfTextRuns) without this.
struct PdfRenderStats {
  int textRunsEmitted = 0;
  int textShowsSkippedUnsupportedFont = 0;
};

struct PdfPoint {
  double x = 0;
  double y = 0;
};

struct PdfDrawCommand {
  enum class Type : uint8_t { StrokePath, FillPath, Image };
  Type type = Type::StrokePath;
  std::vector<PdfPoint> points;
  std::vector<uint8_t> imageData;
  int imageWidth = 0;
  int imageHeight = 0;
  int imageBitsPerComponent = 0;
  int imageComponents = 0;
  int imageX = 0;
  int imageY = 0;
  int imageDrawWidth = 0;
  int imageDrawHeight = 0;
  bool imageIsJpeg = false;
  bool closePath = false;
  bool ink = true;
};

class PdfContentInterpreter {
 public:
  // `resources` is the page's (already-inheritance-resolved) /Resources dict. `scale` converts PDF user-space
  // points to device pixels (uniform, scale-to-fit-width). `llx`/`lly` are the page's MediaBox origin and
  // `pageHeightPts` its MediaBox height, both in points - used to normalize to a 0,0-origin, top-down space.
  // `measureTextWidthPx`, if provided, measures how wide a run will actually render in device pixels (e.g.
  // renderer.text.getWidth) - used to advance the cursor by what was actually drawn instead of by the source
  // PDF font's declared widths, which can differ significantly from our substitute system font's metrics and
  // otherwise cause runs to drift into overlapping/interleaved text within a line. Without it, advance falls
  // back to the (possibly mismatched) declared widths - useful for headless/testing use without a renderer.
  PdfContentInterpreter(
      const PdfDocument& doc, const PdfObject& resources, double scale, double llx, double lly, double pageHeightPts,
      std::function<int(int fontId, const char* utf8Text, EpdFontFamily::Style style)> measureTextWidthPx = nullptr);

  void run(const std::vector<uint8_t>& contentBytes, const std::function<void(const PdfTextRun&)>& onTextRun,
           const std::function<void(const PdfDrawCommand&)>& onDraw = {});

  const PdfRenderStats& stats() const { return stats_; }

 private:
  struct Mat2D {
    double a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;
    static Mat2D multiply(const Mat2D& m1, const Mat2D& m2);
  };

  const PdfDocument& doc_;
  double scale_;
  double llx_;
  double lly_;
  double pageHeightPts_;

  PdfObject fontResourcesDict_;
  PdfObject xObjectResourcesDict_;
  std::map<std::string, PdfFontInfo> fontCache_;
  std::function<int(int, const char*, EpdFontFamily::Style)> measureTextWidthPx_;

  struct GraphicsState {
    Mat2D ctm;
    bool ink = true;
  };
  std::vector<GraphicsState> gsStack_;
  Mat2D ctm_;
  Mat2D tm_;
  Mat2D tlm_;
  double fontSizePts_ = 12.0;
  double charSpace_ = 0.0;
  double wordSpace_ = 0.0;
  double hScale_ = 1.0;
  double leading_ = 0.0;
  double rise_ = 0.0;
  const PdfFontInfo* currentFont_ = nullptr;
  PdfRenderStats stats_;
  std::vector<PdfPoint> path_;
  bool pathClosed_ = false;
  bool ink_ = true;

  const PdfFontInfo* getFont(const std::string& resourceName);
  void showText(const std::string& bytes, const std::function<void(const PdfTextRun&)>& onTextRun);
  void applyOperator(const std::string& op, std::vector<PdfObject>& operands,
                     const std::function<void(const PdfTextRun&)>& onTextRun,
                     const std::function<void(const PdfDrawCommand&)>& onDraw);
  PdfPoint transformPoint(double x, double y) const;
  void finishPath(bool fill, bool stroke, const std::function<void(const PdfDrawCommand&)>& onDraw);
  void showImage(const std::string& resourceName, const std::function<void(const PdfDrawCommand&)>& onDraw);
  static void skipInlineImage(PdfLexer& lexer);
};
