/**
 * @file PdfContentInterpreter.cpp
 * @brief Definitions for PdfContentInterpreter.
 */

#include "PdfContentInterpreter.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>
#include <cmath>

#include "PdfDocument.h"
#include "PdfLexer.h"
#include "PdfValueParser.h"

PdfContentInterpreter::Mat2D PdfContentInterpreter::Mat2D::multiply(const Mat2D& m1, const Mat2D& m2) {
  Mat2D r;
  r.a = m1.a * m2.a + m1.b * m2.c;
  r.b = m1.a * m2.b + m1.b * m2.d;
  r.c = m1.c * m2.a + m1.d * m2.c;
  r.d = m1.c * m2.b + m1.d * m2.d;
  r.e = m1.e * m2.a + m1.f * m2.c + m2.e;
  r.f = m1.e * m2.b + m1.f * m2.d + m2.f;
  return r;
}

PdfPoint PdfContentInterpreter::transformPoint(const double x, const double y) const {
  const double userX = x * ctm_.a + y * ctm_.c + ctm_.e;
  const double userY = x * ctm_.b + y * ctm_.d + ctm_.f;
  return {(userX - llx_) * scale_, (pageHeightPts_ - (userY - lly_)) * scale_};
}

void PdfContentInterpreter::finishPath(const bool fill, const bool stroke,
                                       const std::function<void(const PdfDrawCommand&)>& onDraw) {
  if (onDraw && path_.size() >= 2) {
    if (fill && path_.size() >= 3) {
      PdfDrawCommand command;
      command.type = PdfDrawCommand::Type::FillPath;
      command.points = path_;
      command.closePath = true;
      command.ink = ink_;
      onDraw(command);
    }
    if (stroke) {
      PdfDrawCommand command;
      command.type = PdfDrawCommand::Type::StrokePath;
      command.points = path_;
      command.closePath = pathClosed_;
      command.ink = ink_;
      onDraw(command);
    }
  }
  path_.clear();
  pathClosed_ = false;
}

PdfContentInterpreter::PdfContentInterpreter(
    const PdfDocument& doc, const PdfObject& resources, const double scale, const double llx, const double lly,
    const double pageHeightPts,
    std::function<int(int, const char*, EpdFontFamily::Style)> measureTextWidthPx)
    : doc_(doc),
      scale_(scale),
      llx_(llx),
      lly_(lly),
      pageHeightPts_(pageHeightPts),
      measureTextWidthPx_(std::move(measureTextWidthPx)) {
  const PdfObject resolvedResources = resources.isReference() ? doc_.resolve(resources) : resources;
  if (const PdfObject* fontsEntry = resolvedResources.find("Font")) {
    fontResourcesDict_ = doc_.resolve(*fontsEntry);
  }
  if (const PdfObject* xObjectEntry = resolvedResources.find("XObject")) {
    xObjectResourcesDict_ = doc_.resolve(*xObjectEntry);
  }
}

const PdfFontInfo* PdfContentInterpreter::getFont(const std::string& resourceName) {
  const auto cached = fontCache_.find(resourceName);
  if (cached != fontCache_.end()) return &cached->second;

  const PdfObject* fontRef = fontResourcesDict_.find(resourceName);
  PdfFontInfo info;
  if (fontRef) {
    const PdfObject fontDict = doc_.resolve(*fontRef);
    info = PdfFont::resolve(doc_, fontDict);
  }
  return &fontCache_.emplace(resourceName, std::move(info)).first->second;
}

void PdfContentInterpreter::showImage(const std::string& resourceName,
                                      const std::function<void(const PdfDrawCommand&)>& onDraw) {
  if (!onDraw) return;
  const PdfObject* imageRef = xObjectResourcesDict_.find(resourceName);
  if (!imageRef) return;
  const PdfObject image = doc_.resolve(*imageRef);
  if (!image.isStream()) return;
  const PdfObject* subtype = image.find("Subtype");
  const PdfObject* width = image.find("Width");
  const PdfObject* height = image.find("Height");
  if (!subtype || !subtype->isName() || subtype->strValue != "Image" || !width || !height) return;

  const PdfObject colorSpace = image.find("ColorSpace") ? doc_.resolve(*image.find("ColorSpace")) : PdfObject();
  int components = 1;
  if (colorSpace.isName() && colorSpace.strValue == "DeviceRGB") components = 3;
  if (colorSpace.isName() && colorSpace.strValue != "DeviceGray" && colorSpace.strValue != "DeviceRGB") return;

  const int imageWidth = doc_.resolve(*width).asInt(0);
  const int imageHeight = doc_.resolve(*height).asInt(0);
  const int bits = image.find("BitsPerComponent")
                       ? doc_.resolve(*image.find("BitsPerComponent")).asInt(8)
                       : 8;
  if (imageWidth <= 0 || imageHeight <= 0 || (bits != 1 && bits != 8)) return;

  bool jpeg = false;
  if (const PdfObject* filter = image.find("Filter")) {
    const PdfObject resolvedFilter = doc_.resolve(*filter);
    if (resolvedFilter.isName()) jpeg = resolvedFilter.strValue == "DCTDecode" || resolvedFilter.strValue == "DCT";
    else if (resolvedFilter.isArray() && !resolvedFilter.arrValue.empty()) {
      const PdfObject firstFilter = doc_.resolve(resolvedFilter.arrValue.front());
      jpeg = firstFilter.isName() && (firstFilter.strValue == "DCTDecode" || firstFilter.strValue == "DCT");
    }
  }

  PdfDrawCommand command;
  command.type = PdfDrawCommand::Type::Image;
  command.imageWidth = imageWidth;
  command.imageHeight = imageHeight;
  command.imageBitsPerComponent = bits;
  command.imageComponents = components;
  command.imageIsJpeg = jpeg;
  if (jpeg ? !doc_.getRawStreamBytes(image, command.imageData) : !doc_.getStreamBytes(image, command.imageData)) return;

  const PdfPoint topLeft = transformPoint(0, 1);
  const PdfPoint bottomRight = transformPoint(1, 0);
  command.imageX = static_cast<int>(std::min(topLeft.x, bottomRight.x));
  command.imageY = static_cast<int>(std::min(topLeft.y, bottomRight.y));
  command.imageDrawWidth = std::max(1, static_cast<int>(std::abs(bottomRight.x - topLeft.x)));
  command.imageDrawHeight = std::max(1, static_cast<int>(std::abs(bottomRight.y - topLeft.y)));
  onDraw(command);
}

void PdfContentInterpreter::showText(const std::string& bytes, const std::function<void(const PdfTextRun&)>& onTextRun) {
  if (bytes.empty()) return;
  if (!currentFont_ || !currentFont_->supported) {
    if (currentFont_) stats_.textShowsSkippedUnsupportedFont++;
    return;
  }

  const Mat2D fontScale{fontSizePts_ * hScale_, 0, 0, fontSizePts_, 0, rise_};
  const Mat2D trm = Mat2D::multiply(Mat2D::multiply(fontScale, tm_), ctm_);

  const double normX = trm.e - llx_;
  const double normY = trm.f - lly_;
  const double devX = normX * scale_;
  const double devY = (pageHeightPts_ - normY) * scale_;

  std::string utf8;
  utf8.reserve(bytes.size());
  double declaredAdvance = 0.0;
  int spaceCount = 0;
  size_t charCount = 0;

  if (currentFont_->isCID) {
    for (size_t i = 0; i + 1 < bytes.size(); i += 2) {
      const uint32_t cid = (static_cast<unsigned char>(bytes[i]) << 8) | static_cast<unsigned char>(bytes[i + 1]);
      PdfFont::appendUtf8(utf8, PdfFont::unicodeForCid(*currentFont_, cid));
      const int w1000 = PdfFont::widthForCid(*currentFont_, cid);
      declaredAdvance += ((static_cast<double>(w1000) / 1000.0) * fontSizePts_ + charSpace_) * hScale_;
      charCount++;
    }
  } else {
    for (const unsigned char code : bytes) {
      PdfFont::appendUtf8(utf8, currentFont_->codeToUnicode[code]);
      const int w1000 = PdfFont::widthForCode(*currentFont_, code);
      double tx = (static_cast<double>(w1000) / 1000.0) * fontSizePts_ + charSpace_;
      if (code == ' ') {
        tx += wordSpace_;
        spaceCount++;
      }
      declaredAdvance += tx * hScale_;
      charCount++;
    }
  }

  double totalAdvanceTextSpace = declaredAdvance;

  if (!utf8.empty()) {
    const double userSpaceFontSize = std::hypot(trm.b, trm.d);
    const double devicePixelSize = userSpaceFontSize * scale_;
    const int fontId = PdfFont::nearestBuiltinFontId(devicePixelSize);
    const EpdFontFamily::Style style = currentFont_->style;

    if (measureTextWidthPx_) {
      const int measuredPx = measureTextWidthPx_(fontId, utf8.c_str(), style);
      totalAdvanceTextSpace = (static_cast<double>(measuredPx) / scale_) +
                              static_cast<double>(charCount) * charSpace_ +
                              static_cast<double>(spaceCount) * wordSpace_;
    }

    PdfTextRun run;
    run.x = devX;
    run.y = devY;
    run.sourceFontSizePts = userSpaceFontSize;
    run.fontId = fontId;
    run.style = style;
    run.utf8Text = std::move(utf8);
    stats_.textRunsEmitted++;
    onTextRun(run);
  }

  const Mat2D adv{1, 0, 0, 1, totalAdvanceTextSpace, 0};
  tm_ = Mat2D::multiply(adv, tm_);
}

void PdfContentInterpreter::applyOperator(const std::string& op, std::vector<PdfObject>& operands,
                                          const std::function<void(const PdfTextRun&)>& onTextRun,
                                          const std::function<void(const PdfDrawCommand&)>& onDraw) {
  auto num = [&](size_t idx) -> double { return idx < operands.size() ? operands[idx].asNumber() : 0.0; };

  if (op == "q") {
    gsStack_.push_back({ctm_, ink_});
  } else if (op == "Q") {
    if (!gsStack_.empty()) {
      ctm_ = gsStack_.back().ctm;
      ink_ = gsStack_.back().ink;
      gsStack_.pop_back();
    }
  } else if (op == "cm" && operands.size() >= 6) {
    const Mat2D m{num(0), num(1), num(2), num(3), num(4), num(5)};
    ctm_ = Mat2D::multiply(m, ctm_);
  } else if (op == "m" && operands.size() >= 2) {
    path_.clear();
    pathClosed_ = false;
    path_.push_back(transformPoint(num(0), num(1)));
  } else if (op == "l" && operands.size() >= 2) {
    path_.push_back(transformPoint(num(0), num(1)));
  } else if ((op == "c" && operands.size() >= 6) || (op == "v" && operands.size() >= 4) ||
             (op == "y" && operands.size() >= 4)) {
    const size_t xIndex = op == "c" ? 4 : 2;
    const size_t yIndex = op == "c" ? 5 : 3;
    path_.push_back(transformPoint(num(xIndex), num(yIndex)));
  } else if (op == "h") {
    pathClosed_ = true;
  } else if (op == "re" && operands.size() >= 4) {
    const double x = num(0), y = num(1), w = num(2), h = num(3);
    path_.clear();
    path_.push_back(transformPoint(x, y));
    path_.push_back(transformPoint(x + w, y));
    path_.push_back(transformPoint(x + w, y + h));
    path_.push_back(transformPoint(x, y + h));
    pathClosed_ = true;
  } else if (op == "S" || op == "s") {
    finishPath(false, true, onDraw);
  } else if (op == "f" || op == "F" || op == "f*") {
    finishPath(true, false, onDraw);
  } else if (op == "B" || op == "B*" || op == "b" || op == "b*") {
    if (op == "b" || op == "b*") pathClosed_ = true;
    finishPath(true, true, onDraw);
  } else if (op == "n") {
    path_.clear();
    pathClosed_ = false;
  } else if ((op == "g" || op == "G") && !operands.empty()) {
    ink_ = num(0) < 0.5;
  } else if ((op == "rg" || op == "RG") && operands.size() >= 3) {
    ink_ = (num(0) + num(1) + num(2)) / 3.0 < 0.5;
  } else if ((op == "k" || op == "K") && operands.size() >= 4) {
    const double blackness = std::min(1.0, num(3) + std::min(1.0, num(0) + num(1) + num(2)));
    ink_ = blackness > 0.5;
  } else if (op == "Do" && !operands.empty() && operands.back().isName()) {
    showImage(operands.back().strValue, onDraw);
  } else if (op == "BT") {
    tm_ = Mat2D();
    tlm_ = Mat2D();
  } else if (op == "ET") {
  } else if (op == "Tf" && operands.size() >= 2) {
    const std::string fontName = operands[0].isName() ? operands[0].strValue : "";
    fontSizePts_ = num(1);
    currentFont_ = getFont(fontName);
  } else if (op == "Td" && operands.size() >= 2) {
    const Mat2D t{1, 0, 0, 1, num(0), num(1)};
    tlm_ = Mat2D::multiply(t, tlm_);
    tm_ = tlm_;
  } else if (op == "TD" && operands.size() >= 2) {
    leading_ = -num(1);
    const Mat2D t{1, 0, 0, 1, num(0), num(1)};
    tlm_ = Mat2D::multiply(t, tlm_);
    tm_ = tlm_;
  } else if (op == "Tm" && operands.size() >= 6) {
    tlm_ = Mat2D{num(0), num(1), num(2), num(3), num(4), num(5)};
    tm_ = tlm_;
  } else if (op == "T*") {
    const Mat2D t{1, 0, 0, 1, 0, -leading_};
    tlm_ = Mat2D::multiply(t, tlm_);
    tm_ = tlm_;
  } else if (op == "Tc" && !operands.empty()) {
    charSpace_ = num(0);
  } else if (op == "Tw" && !operands.empty()) {
    wordSpace_ = num(0);
  } else if (op == "Tz" && !operands.empty()) {
    hScale_ = num(0) / 100.0;
  } else if (op == "TL" && !operands.empty()) {
    leading_ = num(0);
  } else if (op == "Ts" && !operands.empty()) {
    rise_ = num(0);
  } else if (op == "Tj" && !operands.empty()) {
    showText(operands.back().strValue, onTextRun);
  } else if (op == "'" && !operands.empty()) {
    const Mat2D t{1, 0, 0, 1, 0, -leading_};
    tlm_ = Mat2D::multiply(t, tlm_);
    tm_ = tlm_;
    showText(operands.back().strValue, onTextRun);
  } else if (op == "\"" && operands.size() >= 3) {
    wordSpace_ = num(0);
    charSpace_ = num(1);
    const Mat2D t{1, 0, 0, 1, 0, -leading_};
    tlm_ = Mat2D::multiply(t, tlm_);
    tm_ = tlm_;
    showText(operands[2].strValue, onTextRun);
  } else if (op == "TJ" && !operands.empty() && operands.back().isArray()) {
    for (const auto& item : operands.back().arrValue) {
      if (item.isString()) {
        showText(item.strValue, onTextRun);
      } else if (item.isNumber()) {
        const double adjTx = -(item.asNumber() / 1000.0) * fontSizePts_ * hScale_;
        const Mat2D adj{1, 0, 0, 1, adjTx, 0};
        tm_ = Mat2D::multiply(adj, tm_);
      }
    }
  }
}

void PdfContentInterpreter::skipInlineImage(PdfLexer& lexer) {
  while (true) {
    const PdfToken t = lexer.next();
    if (t.type == PdfTokenType::Eof) return;
    if (t.type == PdfTokenType::Keyword && t.text == "EI") return;
  }
}

void PdfContentInterpreter::run(const std::vector<uint8_t>& contentBytes,
                                const std::function<void(const PdfTextRun&)>& onTextRun,
                                const std::function<void(const PdfDrawCommand&)>& onDraw) {
  if (contentBytes.empty()) return;

  PdfLexer lexer(contentBytes.data(), contentBytes.size());
  std::vector<PdfObject> operands;
  int tokensSinceYield = 0;
  long totalTokens = 0;
  constexpr long kMaxTokens = 2000000;

  while (true) {
    if (++totalTokens > kMaxTokens) break;
    const PdfToken tok = lexer.next();
    if (tok.type == PdfTokenType::Eof) break;

    if (++tokensSinceYield >= 512) {
      tokensSinceYield = 0;
      vTaskDelay(1);
    }

    if (tok.type == PdfTokenType::Keyword) {
      if (tok.text == "true" || tok.text == "false" || tok.text == "null") {
        operands.push_back(parsePdfValue(lexer, tok));
        continue;
      }
      if (tok.text == "BI") {
        skipInlineImage(lexer);
        operands.clear();
        continue;
      }
      applyOperator(tok.text, operands, onTextRun, onDraw);
      operands.clear();
      continue;
    }

    operands.push_back(parsePdfValue(lexer, tok));
    if (operands.size() > 64) operands.erase(operands.begin());
  }
}
