/**
 * @file Pdf.cpp
 * @brief Definitions for Pdf.
 */

#include "Pdf.h"

#include <GfxRenderer.h>
#include <HardwareSerial.h>
#include <JpegRender.h>
#include <SDCardManager.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>

namespace {

struct MediaBox {
  double llx = 0, lly = 0, urx = 612, ury = 792;
  double width() const { return urx - llx; }
  double height() const { return ury - lly; }
};

MediaBox getMediaBox(const PdfDocument& doc, const PdfObject& pageDict) {
  MediaBox box;
  const PdfObject* mb = pageDict.find("MediaBox");
  if (!mb) return box;
  const PdfObject resolved = doc.resolve(*mb);
  if (!resolved.isArray() || resolved.arrValue.size() < 4) return box;

  double values[4];
  for (int i = 0; i < 4; i++) {
    values[i] = doc.resolve(resolved.arrValue[static_cast<size_t>(i)]).asNumber();
  }
  box.llx = std::min(values[0], values[2]);
  box.urx = std::max(values[0], values[2]);
  box.lly = std::min(values[1], values[3]);
  box.ury = std::max(values[1], values[3]);
  if (box.width() < 1 || box.height() < 1) return MediaBox();
  return box;
}

void drawPdfLine(GfxRenderer& renderer, int x0, int y0, const int x1, const int y1, const bool ink) {
  const int dx = std::abs(x1 - x0);
  const int sx = x0 < x1 ? 1 : -1;
  const int dy = -std::abs(y1 - y0);
  const int sy = y0 < y1 ? 1 : -1;
  int error = dx + dy;
  while (true) {
    renderer.drawPixel(x0, y0, ink);
    if (x0 == x1 && y0 == y1) break;
    const int doubled = 2 * error;
    if (doubled >= dy) {
      error += dy;
      x0 += sx;
    }
    if (doubled <= dx) {
      error += dx;
      y0 += sy;
    }
  }
}

void drawRawPdfImage(GfxRenderer& renderer, const PdfDrawCommand& command, const int offsetX, const int offsetY) {
  const size_t sourceRowBytes = command.imageBitsPerComponent == 1
                                    ? (static_cast<size_t>(command.imageWidth) + 7) / 8
                                    : static_cast<size_t>(command.imageWidth * command.imageComponents);
  if (sourceRowBytes == 0 || command.imageData.size() < sourceRowBytes * static_cast<size_t>(command.imageHeight)) return;

  for (int y = 0; y < command.imageDrawHeight; y++) {
    const int sourceY = std::min(command.imageHeight - 1, y * command.imageHeight / command.imageDrawHeight);
    for (int x = 0; x < command.imageDrawWidth; x++) {
      const int sourceX = std::min(command.imageWidth - 1, x * command.imageWidth / command.imageDrawWidth);
      bool ink = false;
      if (command.imageBitsPerComponent == 1) {
        const uint8_t byte = command.imageData[static_cast<size_t>(sourceY) * sourceRowBytes + sourceX / 8];
        ink = (byte & (0x80 >> (sourceX % 8))) != 0;
      } else {
        const size_t pixel = static_cast<size_t>(sourceY) * sourceRowBytes +
                             static_cast<size_t>(sourceX * command.imageComponents);
        int luminance = 0;
        for (int component = 0; component < command.imageComponents; component++) {
          luminance += command.imageData[pixel + static_cast<size_t>(component)];
        }
        ink = luminance / command.imageComponents < 128;
      }
      renderer.drawPixel(offsetX + command.imageX + x, offsetY + command.imageY + y, ink);
    }
  }
}

}

bool Pdf::load() {
  INX_SERIAL.printf("[%lu] [PDF] Loading PDF: %s\n", millis(), filepath.c_str());

  if (!doc.open(filepath)) {
    INX_SERIAL.printf("[%lu] [PDF] Failed to open (error=%d): %s\n", millis(), static_cast<int>(doc.lastError()),
                  filepath.c_str());
    return false;
  }

  loaded = true;
  INX_SERIAL.printf("[%lu] [PDF] Loaded PDF: %s (%d pages)\n", millis(), filepath.c_str(), doc.getPageCount());
  return true;
}

bool Pdf::clearCache() const {
  if (!SdMan.exists(cachePath.c_str())) return true;
  return SdMan.removeDir(cachePath.c_str());
}

void Pdf::setupCacheDir() const {
  if (SdMan.exists(cachePath.c_str())) return;
  for (size_t i = 1; i < cachePath.length(); i++) {
    if (cachePath[i] == '/') SdMan.mkdir(cachePath.substr(0, i).c_str());
  }
  SdMan.mkdir(cachePath.c_str());
}

std::string Pdf::getTitle() const {
  if (loaded) {
    const std::string title = doc.getTitleFromInfo();
    if (!title.empty()) return title;
  }

  size_t lastSlash = filepath.find_last_of('/');
  size_t lastDot = filepath.find_last_of('.');
  lastSlash = lastSlash == std::string::npos ? 0 : lastSlash + 1;
  if (lastDot == std::string::npos || lastDot <= lastSlash) return filepath.substr(lastSlash);
  return filepath.substr(lastSlash, lastDot - lastSlash);
}

int Pdf::getPageCount() const { return loaded ? doc.getPageCount() : 0; }

std::string Pdf::extractPlainText() const {
  if (!loaded) return "";

  std::string out;
  size_t lineLen = 0;
  constexpr size_t kMaxLineLen = 600;

  const int pageCount = doc.getPageCount();
  for (int i = 0; i < pageCount; i++) {
    bool firstRunOnPage = true;
    double lastY = 0.0;
    renderPage(i, 1.0, [&](const PdfTextRun& run) {
      if (!firstRunOnPage) {
        const double deltaY = run.y - lastY;
        if (deltaY > 24.0 || deltaY < -2.0) {
          out += "\n\n";
          lineLen = 0;
        } else if (deltaY > 2.0) {
          if (lineLen > kMaxLineLen) {
            out += '\n';
            lineLen = 0;
          } else {
            out += ' ';
            lineLen++;
          }
        }
      }
      out += run.utf8Text;
      lineLen += run.utf8Text.size();
      lastY = run.y;
      firstRunOnPage = false;
    });
    out += "\n\n";
    lineLen = 0;
    if (i % 10 == 0) vTaskDelay(1);
  }
  return out;
}

namespace {
bool isDroppableEmpty(const PdfParagraph& p) { return p.words.empty() && !p.isImage(); }
}

std::vector<PdfParagraph> Pdf::extractStyledParagraphs(const int startPage, const int endPage) const {
  std::vector<PdfParagraph> paragraphs;
  if (!loaded) return paragraphs;

  paragraphs.emplace_back();
  std::map<int, int> fontIdHistogram;

  const int pageCount = std::clamp(endPage, 0, doc.getPageCount());
  for (int i = std::max(0, startPage); i < pageCount; i++) {
    bool firstRunOnPage = true;
    double lastY = 0.0;
    double lastFontSizePts = 12.0;
    renderPage(
        i, 1.0,
        [&](const PdfTextRun& run) {
          bool paragraphBreak = false;
          bool lineBreak = false;
          if (!firstRunOnPage) {
            const double deltaY = run.y - lastY;
            const double paragraphGap = std::max(18.0, std::max(run.sourceFontSizePts, lastFontSizePts) * 1.45);
            paragraphBreak = (deltaY > paragraphGap || deltaY < -2.0) && !paragraphs.back().words.empty();
            lineBreak = deltaY > 2.0 || deltaY < -2.0;
            if (paragraphBreak) {
              paragraphs.emplace_back();
            }
          }

          std::string word;
          bool firstWordInRun = true;
          auto flushWord = [&]() {
            if (!word.empty()) {
              PdfStyledWord styledWord;
              styledWord.text = std::move(word);
              styledWord.style = run.style;
              styledWord.sourceFontId = run.fontId;
              styledWord.sourceX = static_cast<int16_t>(std::clamp(std::lround(run.x), -32768L, 32767L));
              styledWord.lineBreakBefore = firstWordInRun && lineBreak && !paragraphBreak;
              paragraphs.back().words.push_back(std::move(styledWord));
              fontIdHistogram[run.fontId]++;
              word.clear();
              firstWordInRun = false;
            }
          };
          for (const char c : run.utf8Text) {
            if (c == ' ') {
              flushWord();
            } else {
              word.push_back(c);
            }
          }
          flushWord();

          lastY = run.y;
          lastFontSizePts = run.sourceFontSizePts > 0 ? run.sourceFontSizePts : lastFontSizePts;
          firstRunOnPage = false;
        },
        nullptr,
        [&](const PdfDrawCommand& command) {
          if (command.type != PdfDrawCommand::Type::Image || !command.imageIsJpeg) return;
          if (!paragraphs.back().words.empty()) paragraphs.emplace_back();
          PdfParagraph imagePara;
          imagePara.imageJpegData = command.imageData;
          imagePara.imageIntrinsicWidth = command.imageWidth;
          imagePara.imageIntrinsicHeight = command.imageHeight;
          paragraphs.push_back(std::move(imagePara));
          paragraphs.emplace_back();
        });
    if (!paragraphs.back().words.empty()) paragraphs.emplace_back();
    if (i % 10 == 0) vTaskDelay(1);
  }
  if (isDroppableEmpty(paragraphs.back())) paragraphs.pop_back();

  int bodyFontId = 0;
  int bodyFontCount = -1;
  for (const auto& entry : fontIdHistogram) {
    if (entry.second > bodyFontCount) {
      bodyFontCount = entry.second;
      bodyFontId = entry.first;
    }
  }

  for (auto& paragraph : paragraphs) {
    if (paragraph.words.empty()) continue;
    size_t largerCount = 0;
    for (const auto& word : paragraph.words) {
      if (word.sourceFontId > bodyFontId) largerCount++;
    }
    paragraph.heading = largerCount * 2 > paragraph.words.size();
  }

  return paragraphs;
}

double Pdf::getPageScale(const int index, const int targetWidthPx, const int targetHeightPx) const {
  if (!loaded) return 1.0;
  PdfObject pageDict;
  if (!doc.getPage(index, pageDict)) return 1.0;
  const MediaBox box = getMediaBox(doc, pageDict);
  const double widthScale = static_cast<double>(targetWidthPx) / box.width();
  const double heightScale = static_cast<double>(targetHeightPx) / box.height();
  if (box.height() * widthScale > static_cast<double>(targetHeightPx) * 1.6) {
    return heightScale;
  }
  return widthScale;
}

int Pdf::getPageHeightPx(const int index, const double scale) const {
  if (!loaded) return 0;
  PdfObject pageDict;
  if (!doc.getPage(index, pageDict)) return 0;
  const MediaBox box = getMediaBox(doc, pageDict);
  return static_cast<int>(box.height() * scale);
}

bool Pdf::renderPage(const int index, const double scale, const std::function<void(const PdfTextRun&)>& onTextRun,
                     PdfRenderStats* outStats, const std::function<void(const PdfDrawCommand&)>& onDraw,
                     const std::function<int(int, const char*, EpdFontFamily::Style)>& measureTextWidthPx) const {
  if (!loaded) return false;

  INX_SERIAL.printf("[%lu] [PDF-DBG] renderPage(%d): getPage...\n", millis(), index);
  PdfObject pageDict;
  if (!doc.getPage(index, pageDict)) return false;

  const MediaBox box = getMediaBox(doc, pageDict);
  INX_SERIAL.printf("[%lu] [PDF-DBG] renderPage(%d): mediaBox %.1fx%.1f, scale=%.3f, resolving resources...\n",
                millis(), index, box.width(), box.height(), scale);

  const PdfObject* resourcesObj = pageDict.find("Resources");
  PdfObject resources = resourcesObj ? doc.resolve(*resourcesObj) : PdfObject();
  INX_SERIAL.printf("[%lu] [PDF-DBG] renderPage(%d): resources resolved, resolving contents...\n", millis(), index);

  const PdfObject* contentsObj = pageDict.find("Contents");
  if (!contentsObj) return true;

  std::vector<uint8_t> contentBytes;
  const PdfObject contents = doc.resolve(*contentsObj);
  if (contents.isStream()) {
    if (!doc.getStreamBytes(contents, contentBytes)) {
      INX_SERIAL.printf("[%lu] [PDF] Failed to decode page %d content stream\n", millis(), index + 1);
      return false;
    }
  } else if (contents.isArray()) {
    for (const auto& item : contents.arrValue) {
      const PdfObject streamObj = doc.resolve(item);
      std::vector<uint8_t> part;
      if (streamObj.isStream() && doc.getStreamBytes(streamObj, part)) {
        contentBytes.insert(contentBytes.end(), part.begin(), part.end());
        contentBytes.push_back(' ');
      }
    }
  }
  INX_SERIAL.printf("[%lu] [PDF-DBG] renderPage(%d): %zu decoded content bytes, interpreting...\n", millis(), index,
                contentBytes.size());

  PdfContentInterpreter interpreter(doc, resources, scale, box.llx, box.lly, box.height(), measureTextWidthPx);
  interpreter.run(contentBytes, onTextRun, onDraw);
  INX_SERIAL.printf("[%lu] [PDF-DBG] renderPage(%d): interpretation done\n", millis(), index);
  if (outStats) *outStats = interpreter.stats();
  return true;
}

bool Pdf::renderPageToRenderer(const int index, GfxRenderer& renderer, const int offsetX, const int offsetY,
                               const int targetWidthPx, const int targetHeightPx, PdfRenderStats* outStats) const {
  const double scale = getPageScale(index, targetWidthPx, targetHeightPx);
  return renderPage(
      index, scale,
      [&](const PdfTextRun& run) {
        const int lineTopY =
            offsetY + static_cast<int>(run.y) - renderer.text.getFontAscenderSize(run.fontId);
        renderer.text.render(run.fontId, offsetX + static_cast<int>(run.x), lineTopY, run.utf8Text.c_str(), true,
                             run.style);
      },
      outStats,
      [&](const PdfDrawCommand& command) {
        if (command.type == PdfDrawCommand::Type::Image) {
          if (command.imageIsJpeg) {
            setupCacheDir();
            const std::string tempPath = cachePath + "/active-image.jpg";
            FsFile imageFile;
            if (SdMan.openFileForWrite("PDF", tempPath, imageFile)) {
              imageFile.write(command.imageData.data(), command.imageData.size());
              imageFile.close();
              JpegRender(renderer).fromPath(tempPath, offsetX + command.imageX, offsetY + command.imageY,
                                            command.imageDrawWidth, command.imageDrawHeight, false,
                                            ImageRenderMode::OneBit);
            }
          } else {
            drawRawPdfImage(renderer, command, offsetX, offsetY);
          }
          return;
        }
        if (command.points.size() < 2) return;
        if (command.type == PdfDrawCommand::Type::FillPath) {
          std::vector<int> x;
          std::vector<int> y;
          x.reserve(command.points.size());
          y.reserve(command.points.size());
          for (const auto& point : command.points) {
            x.push_back(offsetX + static_cast<int>(point.x));
            y.push_back(offsetY + static_cast<int>(point.y));
          }
          renderer.polygon.render(x.data(), y.data(), static_cast<int>(x.size()), true, command.ink);
          return;
        }
        for (size_t i = 1; i < command.points.size(); i++) {
          drawPdfLine(renderer, offsetX + static_cast<int>(command.points[i - 1].x),
                      offsetY + static_cast<int>(command.points[i - 1].y),
                      offsetX + static_cast<int>(command.points[i].x),
                      offsetY + static_cast<int>(command.points[i].y), command.ink);
        }
        if (command.closePath) {
          drawPdfLine(renderer, offsetX + static_cast<int>(command.points.back().x),
                      offsetY + static_cast<int>(command.points.back().y),
                      offsetX + static_cast<int>(command.points.front().x),
                      offsetY + static_cast<int>(command.points.front().y), command.ink);
        }
      },
      [&](const int fontId, const char* utf8Text, const EpdFontFamily::Style style) {
        return renderer.text.getWidth(fontId, utf8Text, style);
      });
}

std::string Pdf::getCoverBmpPath() const { return cachePath + "/cover.bmp"; }
std::string Pdf::getThumbBmpPath() const { return cachePath + "/thumb.bmp"; }

namespace {

bool writeFramebufferBmp(const GfxRenderer& renderer, const std::string& path, const int width, const int height) {
  FsFile bmp;
  if (!SdMan.openFileForWrite("PDF", path, bmp)) return false;

  const uint32_t rowSize = ((static_cast<uint32_t>(width) + 31) / 32) * 4;
  const uint32_t imageSize = rowSize * static_cast<uint32_t>(height);
  const uint32_t fileSize = 14 + 40 + 8 + imageSize;

  bmp.write('B');
  bmp.write('M');
  bmp.write(reinterpret_cast<const uint8_t*>(&fileSize), 4);
  uint32_t reserved = 0;
  bmp.write(reinterpret_cast<const uint8_t*>(&reserved), 4);
  uint32_t dataOffset = 14 + 40 + 8;
  bmp.write(reinterpret_cast<const uint8_t*>(&dataOffset), 4);

  uint32_t dibHeaderSize = 40;
  bmp.write(reinterpret_cast<const uint8_t*>(&dibHeaderSize), 4);
  int32_t w = width;
  bmp.write(reinterpret_cast<const uint8_t*>(&w), 4);
  int32_t h = -height;
  bmp.write(reinterpret_cast<const uint8_t*>(&h), 4);
  uint16_t planes = 1;
  bmp.write(reinterpret_cast<const uint8_t*>(&planes), 2);
  uint16_t bitsPerPixel = 1;
  bmp.write(reinterpret_cast<const uint8_t*>(&bitsPerPixel), 2);
  uint32_t compression = 0;
  bmp.write(reinterpret_cast<const uint8_t*>(&compression), 4);
  bmp.write(reinterpret_cast<const uint8_t*>(&imageSize), 4);
  int32_t ppm = 2835;
  bmp.write(reinterpret_cast<const uint8_t*>(&ppm), 4);
  bmp.write(reinterpret_cast<const uint8_t*>(&ppm), 4);
  uint32_t colors = 2;
  bmp.write(reinterpret_cast<const uint8_t*>(&colors), 4);
  bmp.write(reinterpret_cast<const uint8_t*>(&colors), 4);

  uint8_t black[4] = {0x00, 0x00, 0x00, 0x00};
  bmp.write(black, 4);
  uint8_t white[4] = {0xFF, 0xFF, 0xFF, 0x00};
  bmp.write(white, 4);

  const size_t srcRowBytes = (static_cast<size_t>(width) + 7) / 8;
  std::vector<uint8_t> row(rowSize, 0xFF);
  for (int y = 0; y < height; y++) {
    std::fill(row.begin(), row.end(), 0xFF);
    renderer.readPackedRow1bpp(0, y, width, row.data());
    for (size_t i = 0; i < srcRowBytes; i++) row[i] = static_cast<uint8_t>(~row[i]);
    bmp.write(row.data(), rowSize);
  }

  bmp.close();
  return true;
}

}

bool Pdf::generateCoverBmp(GfxRenderer& renderer) const {
  if (SdMan.exists(getCoverBmpPath().c_str())) return true;
  if (!loaded || doc.getPageCount() == 0) return false;

  setupCacheDir();

  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  renderer.clearScreen();
  renderPageToRenderer(0, renderer, 0, 0, width, height);

  const bool ok = writeFramebufferBmp(renderer, getCoverBmpPath(), width, height);
  INX_SERIAL.printf("[%lu] [PDF] Generated cover BMP: %s ok=%d\n", millis(), getCoverBmpPath().c_str(), ok);
  return ok;
}

bool Pdf::generateThumbBmp(GfxRenderer& renderer) const {
  if (SdMan.exists(getThumbBmpPath().c_str())) return true;
  if (!generateCoverBmp(renderer)) return false;

  FsFile src;
  FsFile dst;
  if (!SdMan.openFileForRead("PDF", getCoverBmpPath(), src) || !SdMan.openFileForWrite("PDF", getThumbBmpPath(), dst)) {
    if (src) src.close();
    if (dst) dst.close();
    return false;
  }
  uint8_t buffer[2048];
  while (src.available()) {
    const size_t bytesRead = src.read(buffer, sizeof(buffer));
    dst.write(buffer, bytesRead);
  }
  src.close();
  dst.close();
  return SdMan.exists(getThumbBmpPath().c_str());
}
