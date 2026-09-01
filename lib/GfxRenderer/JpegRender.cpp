/**
 * @file JpegRender.cpp
 * @brief Definitions for JpegRender.
 */

#include "JpegRender.h"

#include <SDCardManager.h>
#include <picojpeg.h>
#include <toojpeg.h>
#include <BoardConfig.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

#include <esp_heap_caps.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
static void* stbiTransientAlloc(const size_t bytes) { return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
static void* stbiTransientRealloc(void* pointer, const size_t bytes) {
  return heap_caps_realloc(pointer, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
static void stbiTransientFree(void* pointer) { heap_caps_free(pointer); }

#define STBI_ONLY_JPEG
#define STBI_ONLY_GIF
#define STBI_ONLY_GIF
#define STBI_NO_STDIO
#define STBI_MALLOC(bytes) stbiTransientAlloc(bytes)
#define STBI_REALLOC(pointer, bytes) stbiTransientRealloc(pointer, bytes)
#define STBI_FREE(pointer) stbiTransientFree(pointer)
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#undef STB_IMAGE_IMPLEMENTATION
#undef STBI_FREE
#undef STBI_REALLOC
#undef STBI_MALLOC
#undef STBI_NO_STDIO
#undef STBI_ONLY_GIF
#undef STBI_ONLY_GIF
#undef STBI_ONLY_JPEG

#include "BitmapUtil.h"
#include "StbImageDecode.h"
#include "GfxRenderer.h"
#include "../../src/system/EpubPerf.h"

#pragma GCC optimize("O2")

namespace {
constexpr uint32_t kImageServiceTimeBudgetMs = 16;

class ImageServiceBudget {
 public:
  void service() {
    if (millis() - lastServiceAt_ < kImageServiceTimeBudgetMs) {
      return;
    }
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(1));
    lastServiceAt_ = millis();
  }

 private:
  uint32_t lastServiceAt_ = millis();
};

uint8_t* allocateJpegIoBuffer(const size_t bytes) {
  if (auto* internal = static_cast<uint8_t*>(heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT))) {
    return internal;
  }
  return static_cast<uint8_t*>(heap_caps_malloc(bytes, MALLOC_CAP_8BIT));
}

void freeJpegIoBuffer(uint8_t* buffer) {
  heap_caps_free(buffer);
}

struct JpegReadContext {
  FsFile& file;
  uint8_t* buffer;
  size_t bufferSize;
  size_t bufferPos;
  size_t bufferFilled;
};

struct PicoJpegDecodeGuard {
  bool active = true;
  void release() {
    if (active) {
      pjpeg_decode_deinit();
      active = false;
    }
  }
  ~PicoJpegDecodeGuard() { release(); }
};

unsigned char jpegReadCallback(unsigned char* pBuf, unsigned char bufSize, unsigned char* pBytesActRead,
                               void* pCallbackData) {
  auto* context = static_cast<JpegReadContext*>(pCallbackData);
  if (!context || !context->file || !context->buffer) {
    return PJPG_STREAM_READ_ERROR;
  }
  if (context->bufferPos >= context->bufferFilled) {
    context->bufferFilled = context->file.read(context->buffer, context->bufferSize);
    context->bufferPos = 0;
    if (context->bufferFilled == 0) {
      *pBytesActRead = 0;
      return 0;
    }
  }
  const size_t available = context->bufferFilled - context->bufferPos;
  const size_t toRead = std::min(static_cast<size_t>(bufSize), available);
  memcpy(pBuf, context->buffer + context->bufferPos, toRead);
  context->bufferPos += toRead;
  *pBytesActRead = static_cast<unsigned char>(toRead);
  return 0;
}

bool scanJpegHeader(FsFile& file, int* outWidth = nullptr, int* outHeight = nullptr,
                    bool* outProgressive = nullptr) {
  const uint32_t originalPos = file.position();
  file.seek(0);

  uint8_t buf[256];
  size_t bufLen = 0;
  size_t bufPos = 0;
  bool foundSof = false;

  auto logicalPos = [&]() -> uint32_t { return file.position() - static_cast<uint32_t>(bufLen - bufPos); };
  auto nextByte = [&](uint8_t& out) -> bool {
    if (bufPos >= bufLen) {
      bufLen = file.read(buf, sizeof(buf));
      bufPos = 0;
      if (bufLen == 0) return false;
    }
    out = buf[bufPos++];
    return true;
  };
  auto skip = [&](const uint32_t count) {
    file.seek(logicalPos() + count);
    bufLen = 0;
    bufPos = 0;
  };

  uint8_t b;
  while (nextByte(b)) {
    if (b != 0xFF) continue;
    if (!nextByte(b)) break;
    while (b == 0xFF) {
      if (!nextByte(b)) break;
    }
    const uint8_t marker = b;
    const bool isSof = (marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC);
    if (isSof) {
      uint8_t lenHi;
      uint8_t lenLo;
      uint8_t precision;
      uint8_t heightHi;
      uint8_t heightLo;
      uint8_t widthHi;
      uint8_t widthLo;
      if (!nextByte(lenHi) || !nextByte(lenLo) || !nextByte(precision) || !nextByte(heightHi) ||
          !nextByte(heightLo) || !nextByte(widthHi) || !nextByte(widthLo)) {
        break;
      }
      (void)precision;
      const uint16_t segmentLength = (static_cast<uint16_t>(lenHi) << 8) | lenLo;
      const int width = (static_cast<int>(widthHi) << 8) | widthLo;
      const int height = (static_cast<int>(heightHi) << 8) | heightLo;
      if (segmentLength < 7 || width <= 0 || height <= 0) break;
      if (outWidth) *outWidth = width;
      if (outHeight) *outHeight = height;
      if (outProgressive) *outProgressive = marker == 0xC2 || marker == 0xC9 || marker == 0xCA;
      foundSof = true;
      break;
    }
    if (marker != 0xD8 && marker != 0xD9 && marker != 0x01 && !(marker >= 0xD0 && marker <= 0xD7)) {
      uint8_t lenHi;
      uint8_t lenLo;
      if (!nextByte(lenHi) || !nextByte(lenLo)) break;
      const uint16_t len = (static_cast<uint16_t>(lenHi) << 8) | lenLo;
      if (len < 2) break;
      skip(len - 2);
    }
  }
  file.seek(originalPos);
  return foundSof;
}

bool isUnsupportedJpeg(FsFile& file) {
  bool progressive = false;
  scanJpegHeader(file, nullptr, nullptr, &progressive);
  return progressive;
}

inline uint8_t grayFromRgb(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<uint8_t>((77 * static_cast<int>(r) + 150 * static_cast<int>(g) + 29 * static_cast<int>(b) + 128) >>
                              8);
}

constexpr int kJpegDitherSolidBlackMax = 20;
constexpr int kJpegDitherSolidWhiteMin = 255;
constexpr int kJpegTwoBitSolidBlackMax = 10;
constexpr int kJpegTwoBitSolidWhiteMin = 224;
constexpr int kJpegTwoBitContrastPercent = 120;
constexpr int kJpegTwoBitSharpenThreshold = 18;
constexpr int kJpegTwoBitSharpenPercent = 80;
constexpr int kJpegTwoBitSharpenMax = 130;
constexpr int kJpegTwoBitEdgeThreshold = 0;
constexpr int kJpegTwoBitEdgeMaxDarken = 0;
constexpr int kJpegTwoBitHighlightThreshold = 5;
constexpr int kJpegTwoBitHighlightMaxLift = 50;
constexpr int kJpegTwoBitShadowStart = 1;
constexpr int kJpegTwoBitShadowMaxDarken = 0;
constexpr int kJpegTwoBitShadowTextureLiftMin = 42;
constexpr int kJpegTwoBitShadowTextureLiftMax = 126;
constexpr int kJpegTwoBitShadowTextureLift = 10;
constexpr int kJpegTwoBitMidtoneLiftMin = 96;
constexpr int kJpegTwoBitMidtoneLiftMax = 184;
constexpr int kJpegTwoBitMidtoneLift = 8;
constexpr int kJpegTwoBitFlatShadowTextureLift = 4;
constexpr int kJpegTwoBitMediumMixStart = 96;
constexpr int kJpegTwoBitMediumMixFull = 148;
constexpr int kJpegTwoBitMediumMixDetailMin = 2;
constexpr int kJpegTwoBitMediumMixDetailFull = 28;
constexpr int kJpegTwoBitQualitySolidBlackMax = 12;
constexpr int kJpegTwoBitQualitySolidWhiteMin = 218;
constexpr int kJpegTwoBitQualityContrastPercent = 162;
constexpr int kJpegTwoBitQualityShadowContrastPercent = 122;
constexpr int kJpegTwoBitQualitySharpenThreshold = 3;
constexpr int kJpegTwoBitQualitySharpenPercent = 105;
constexpr int kJpegTwoBitQualitySharpenMax = 38;
constexpr int kJpegTwoBitQualityShadowKnee = 96;
constexpr int kJpegTwoBitQualityShadowDarkenMax = 6;
constexpr int kJpegTwoBitQualityMicroDither = 8;

int jpegTwoBitTone(const int gray) {
  const int adjusted = ((gray - 128) * kJpegTwoBitContrastPercent) / 100 + 128;
  return std::max(0, std::min(255, adjusted));
}

int jpegTwoBitDetailTone(const int gray, const int leftGray, const int rightGray, const int x, const int y) {
  const int neighbor = (leftGray + rightGray) / 2;
  const int detail = gray - neighbor;
  const int darkEdge = neighbor - gray;
  const int lightEdge = gray - neighbor;
  int sharpenedGray = gray;
  if (std::abs(detail) > kJpegTwoBitSharpenThreshold) {
    const int boost =
        std::max(-kJpegTwoBitSharpenMax, std::min(kJpegTwoBitSharpenMax, (detail * kJpegTwoBitSharpenPercent) / 100));
    sharpenedGray = std::max(0, std::min(255, gray + boost));
  }

  int tone = jpegTwoBitTone(sharpenedGray);
  if (gray < kJpegTwoBitShadowStart) {
    const int shadowDarken = ((kJpegTwoBitShadowStart - gray) * kJpegTwoBitShadowMaxDarken) / kJpegTwoBitShadowStart;
    tone = std::max(0, tone - shadowDarken);
  }
  if (lightEdge > kJpegTwoBitHighlightThreshold) {
    const int lift = std::min(kJpegTwoBitHighlightMaxLift, (lightEdge - kJpegTwoBitHighlightThreshold) * 3);
    tone = std::max(tone, jpegTwoBitTone(std::min(255, gray + lift)));
  }
  if (darkEdge > kJpegTwoBitEdgeThreshold) {
    const int edgeDarken = std::min(kJpegTwoBitEdgeMaxDarken, darkEdge - kJpegTwoBitEdgeThreshold);
    tone = std::max(0, tone - edgeDarken);
  }
  if (gray >= kJpegTwoBitShadowTextureLiftMin && gray <= kJpegTwoBitShadowTextureLiftMax) {
    tone = std::min(255, tone + kJpegTwoBitShadowTextureLift);
    if (std::abs(detail) <= kJpegTwoBitMediumMixDetailFull) {
      const int lattice = (x * 37 + y * 17 + ((x ^ y) * 11)) & 15;
      const int flatness = kJpegTwoBitMediumMixDetailFull - std::max(std::abs(detail), kJpegTwoBitMediumMixDetailMin);
      const int lift = (lattice * kJpegTwoBitFlatShadowTextureLift * flatness) /
                       (15 * (kJpegTwoBitMediumMixDetailFull - kJpegTwoBitMediumMixDetailMin));
      tone = std::min(255, tone + lift);
    }
  }
  if (gray >= kJpegTwoBitMidtoneLiftMin && gray <= kJpegTwoBitMidtoneLiftMax) {
    tone = std::min(255, tone + kJpegTwoBitMidtoneLift);
  }
  return std::max(0, std::min(255, tone));
}

int jpegQualityToneCommon(const int gray, const int leftGray, const int rightGray, const int x, const int y,
                          const int shadowLiftPerKnee) {
  if (gray <= kJpegTwoBitQualitySolidBlackMax) {
    return 0;
  }
  if (gray >= kJpegTwoBitQualitySolidWhiteMin) {
    return 255;
  }

  const int neighbor = (leftGray + rightGray) / 2;
  const int detail = gray - neighbor;
  int sharpenedGray = gray;
  if (std::abs(detail) > kJpegTwoBitQualitySharpenThreshold) {
    const int boost =
        std::max(-kJpegTwoBitQualitySharpenMax,
                 std::min(kJpegTwoBitQualitySharpenMax, (detail * kJpegTwoBitQualitySharpenPercent) / 100));
    sharpenedGray = std::max(0, std::min(255, gray + boost));
  }

  int tone;
  if (sharpenedGray < 128) {
    tone = ((sharpenedGray - 64) * kJpegTwoBitQualityShadowContrastPercent) / 100 + 64;
  } else {
    tone = ((sharpenedGray - 128) * kJpegTwoBitQualityContrastPercent) / 100 + 128;
  }
  if (gray < kJpegTwoBitQualityShadowKnee) {
    const int kneeDepth = kJpegTwoBitQualityShadowKnee - gray;
    tone += (kneeDepth * shadowLiftPerKnee) / kJpegTwoBitQualityShadowKnee;
  }

  if (tone <= 8) {
    return 0;
  }
  if (tone >= 238) {
    return 255;
  }

  if (gray > kJpegTwoBitQualitySolidBlackMax + 10 && gray < kJpegTwoBitQualitySolidWhiteMin - 10) {
    const int latticeA = ((x * 13 + y * 7 + ((x ^ y) * 3)) & 15) - 8;
    const int latticeB = (((x + y * 3) * 5) & 7) - 4;
    tone += ((latticeA + latticeB) * kJpegTwoBitQualityMicroDither) / 12;
  }

  return std::max(0, std::min(255, tone));
}

int jpegTone(const int gray, const int leftGray, const int rightGray, const int x, const int y, const bool quality) {
  const int tone = quality ? jpegQualityToneCommon(gray, leftGray, rightGray, x, y,
                                                   -kJpegTwoBitQualityShadowDarkenMax)
                           : jpegTwoBitDetailTone(gray, leftGray, rightGray, x, y);
  if (quality && BoardConfig::ACTIVE.displayController == BoardConfig::DisplayController::UC8179) {
    return tone;
  }
  return applyDeviceToneCurve(tone);
}

int darkenOneBitJpegGray(const int gray) { return std::max(0, gray - 22); }

int quantizeGray(const int corrected, const ImageRenderMode mode) {
  if (mode == ImageRenderMode::TwoBit) {
    return quantizeTwoBitImage(corrected).value;
  }
  return corrected < 128 ? 0 : 255;
}

void drawPixelForLevel(const GfxRenderer& renderer, const int x, const int y, const uint8_t level) {
  const GfxRenderer::RenderMode renderMode = renderer.getRenderMode();
  const uint8_t grayscaleCode = grayscaleCodeTable()[level & 3];
  if (renderMode == GfxRenderer::BW) {
    if (level > 0) {
      renderer.drawPixel(x, y, true);
    }
  } else if (renderMode == GfxRenderer::GRAYSCALE_MSB && ((grayscaleCode & 0b10) != 0)) {
    renderer.drawPixel(x, y, false);
  } else if (renderMode == GfxRenderer::GRAYSCALE_LSB && ((grayscaleCode & 0b01) != 0)) {
    renderer.drawPixel(x, y, false);
  } else if (renderMode == GfxRenderer::GRAY2_LSB || renderMode == GfxRenderer::GRAY2_MSB) {
    const uint8_t bit = (renderMode == GfxRenderer::GRAY2_LSB) ? 0b01 : 0b10;
    const bool bitSet = (mapQualityGray2Level(level) & bit) != 0;
    const bool invert =
        BoardConfig::ACTIVE.displayController == BoardConfig::DisplayController::UC8179;
    if (bitSet == invert) {
      renderer.drawPixel(x, y, true);
    }
  }
}

void drawQuantizedPixel(const GfxRenderer& renderer, const int x, const int y, const int q,
                        const ImageRenderMode mode) {
  if (mode == ImageRenderMode::OneBit) {
    if (q == 0) {
      renderer.drawPixel(x, y, true);
    }
    return;
  }
  drawPixelForLevel(renderer, x, y, adjustTwoBitImageLevelForDisplay(FourToneImageDitherer::levelFromValue(q)));
}

struct ProgressiveHuffman {
  uint8_t counts[17] = {};
  uint16_t firstCode[17] = {};
  uint16_t firstIndex[17] = {};
  uint8_t symbols[256] = {};
  bool ready = false;
};

struct ProgressiveJpeg {
  FsFile& file;
  uint8_t buffer[512] = {};
  uint32_t bufferLength = 0;
  uint32_t bufferPosition = 0;
  uint32_t bitBuffer = 0;
  int bitCount = 0;
  int pendingMarker = 0;
  bool ioError = false;

  int rawByte() {
    if (bufferPosition >= bufferLength) {
      const size_t n = file.read(buffer, sizeof(buffer));
      if (n == 0) {
        ioError = true;
        return -1;
      }
      bufferLength = static_cast<uint32_t>(n);
      bufferPosition = 0;
    }
    return buffer[bufferPosition++];
  }

  int nextBit() {
    if (bitCount == 0) {
      int value = rawByte();
      if (value < 0) return -1;
      if (value == 0xFF) {
        int marker = rawByte();
        while (marker == 0xFF) marker = rawByte();
        if (marker < 0) return -1;
        if (marker != 0x00) {
          pendingMarker = marker;
          return -1;
        }
        value = 0xFF;
      }
      bitBuffer = static_cast<uint32_t>(value);
      bitCount = 8;
    }
    --bitCount;
    return static_cast<int>((bitBuffer >> bitCount) & 1u);
  }

  int decodeHuffman(const ProgressiveHuffman& table) {
    uint32_t code = 0;
    for (int length = 1; length <= 16; ++length) {
      const int value = nextBit();
      if (value < 0) return -1;
      code = (code << 1) | static_cast<uint32_t>(value);
      if (table.counts[length] != 0 &&
          code < static_cast<uint32_t>(table.firstCode[length]) + table.counts[length]) {
        return table.symbols[table.firstIndex[length] + (code - table.firstCode[length])];
      }
    }
    return -1;
  }

  int32_t receiveExtend(const int size, bool& ok) {
    int32_t value = 0;
    for (int i = 0; i < size; ++i) {
      const int bitValue = nextBit();
      if (bitValue < 0) {
        ok = false;
        return 0;
      }
      value = (value << 1) | bitValue;
    }
    if (size > 0 && value < (1 << (size - 1))) value -= (1 << size) - 1;
    return value;
  }
};

void buildProgressiveHuffman(ProgressiveHuffman& table) {
  uint16_t code = 0;
  uint16_t index = 0;
  for (int length = 1; length <= 16; ++length) {
    table.firstCode[length] = code;
    table.firstIndex[length] = index;
    code = static_cast<uint16_t>(code + table.counts[length]);
    index = static_cast<uint16_t>(index + table.counts[length]);
    code = static_cast<uint16_t>(code << 1);
  }
  table.ready = true;
}

using ProgressiveRowCallback = bool (*)(void* context, uint16_t row, const uint8_t* pixels, uint16_t width);

struct ProgressiveResampler {
  uint16_t sourceWidth = 0;
  uint16_t sourceHeight = 0;
  uint16_t outputWidth = 0;
  uint16_t outputHeight = 0;
  uint8_t* previousRow = nullptr;
  uint8_t* currentRow = nullptr;
  uint8_t* outputRow = nullptr;
  int32_t previousSourceY = -1;
  uint32_t nextOutputRow = 0;
  ProgressiveRowCallback callback = nullptr;
  void* callbackContext = nullptr;
  bool stopped = false;

  static uint32_t stepFp(const uint16_t source, const uint16_t output) {
    return (source <= 1 || output <= 1) ? 0 : ((static_cast<uint32_t>(source - 1) << 16) / (output - 1));
  }

  void scaleRow(const uint8_t* source, uint8_t* output) const {
    const uint32_t step = stepFp(sourceWidth, outputWidth);
    for (uint32_t x = 0; x < outputWidth; ++x) {
      const uint32_t fp = x + 1 == outputWidth ? static_cast<uint32_t>(sourceWidth - 1) << 16 : x * step;
      const uint32_t x0 = fp >> 16;
      const uint32_t x1 = x0 + 1 < sourceWidth ? x0 + 1 : x0;
      const uint32_t fraction = fp & 0xFFFFu;
      output[x] = static_cast<uint8_t>((source[x0] * (0x10000u - fraction) + source[x1] * fraction) >> 16);
    }
  }

  bool feed(const uint8_t* sourceRow, const uint32_t sourceY) {
    scaleRow(sourceRow, currentRow);
    const uint32_t step = stepFp(sourceHeight, outputHeight);
    while (nextOutputRow < outputHeight) {
      const uint32_t fp = nextOutputRow + 1 == outputHeight
                              ? static_cast<uint32_t>(sourceHeight - 1) << 16
                              : nextOutputRow * step;
      const uint32_t y0 = fp >> 16;
      const uint32_t y1 = y0 + 1 < sourceHeight ? y0 + 1 : y0;
      if (y1 > sourceY) break;
      const uint8_t* row0 = y0 == sourceY ? currentRow : previousRow;
      const uint8_t* row1 = y1 == sourceY ? currentRow : previousRow;
      const uint32_t fraction = fp & 0xFFFFu;
      for (uint32_t x = 0; x < outputWidth; ++x) {
        outputRow[x] = static_cast<uint8_t>((row0[x] * (0x10000u - fraction) + row1[x] * fraction) >> 16);
      }
      if (!callback(callbackContext, static_cast<uint16_t>(nextOutputRow), outputRow, outputWidth)) {
        stopped = true;
        return false;
      }
      ++nextOutputRow;
    }
    std::swap(previousRow, currentRow);
    previousSourceY = static_cast<int32_t>(sourceY);
    return true;
  }

  bool finish() {
    while (nextOutputRow < outputHeight && previousSourceY >= 0) {
      if (!callback(callbackContext, static_cast<uint16_t>(nextOutputRow), previousRow, outputWidth)) {
        stopped = true;
        return false;
      }
      ++nextOutputRow;
    }
    return true;
  }
};

struct ProgressiveRenderOutput {
  GfxRenderer& renderer;
  int drawX;
  int drawY;
  ImageRenderMode mode;
  bool quality;
  FourToneImageDitherer* twoBitDitherer;
  Atkinson1BitDitherer* oneBitDitherer;
  JpegLevelCapture* capture;

  static bool emit(void* context, const uint16_t row, const uint8_t* pixels, const uint16_t width) {
    auto* output = static_cast<ProgressiveRenderOutput*>(context);
    if ((row & 7u) == 0u) {
      esp_task_wdt_reset();
      yield();
    }
    const int screenY = output->drawY + row;
    for (uint16_t x = 0; x < width; ++x) {
      const int gray = pixels[x];
      int quantized = 0;
      if (gray <= (output->mode == ImageRenderMode::TwoBit ? kJpegTwoBitSolidBlackMax : kJpegDitherSolidBlackMax)) {
        quantized = 0;
      } else if (gray >=
                 (output->mode == ImageRenderMode::TwoBit ? kJpegTwoBitSolidWhiteMin : kJpegDitherSolidWhiteMin)) {
        quantized = 255;
      } else if (output->mode == ImageRenderMode::TwoBit) {
        const int left = x > 0 ? pixels[x - 1] : gray;
        const int right = x + 1 < width ? pixels[x + 1] : gray;
        const int tone = jpegTone(gray, left, right, output->drawX + x, screenY, output->quality);
        quantized = output->quality ? output->twoBitDitherer->processQuality(tone, x).value
                                    : output->twoBitDitherer->process(tone, x).value;
      } else {
        quantized = output->oneBitDitherer->processPixel(darkenOneBitJpegGray(gray), x) ? 255 : 0;
      }

      if (output->mode == ImageRenderMode::TwoBit) {
        const uint8_t level = adjustTwoBitImageLevelForDisplay(FourToneImageDitherer::levelFromValue(quantized));
        if (output->capture && output->capture->captured) {
          const size_t pixelIndex = static_cast<size_t>(row) * width + x;
          output->capture->values[pixelIndex / 4] |= static_cast<uint8_t>((level & 3) << ((pixelIndex % 4) * 2));
        }
        drawPixelForLevel(output->renderer, output->drawX + x, screenY, level);
      } else if (quantized == 0) {
        output->renderer.drawPixel(output->drawX + x, screenY, true);
      }
    }
    if (output->mode == ImageRenderMode::TwoBit) {
      output->twoBitDitherer->nextRow();
    } else {
      output->oneBitDitherer->nextRow();
    }
    return true;
  }
};

struct StbiFileContext {
  FsFile& file;
};

int stbiRead(void* user, char* data, int size);
void stbiSkip(void* user, int count);
int stbiEof(void* user);

struct ProgressiveDecodeJob {
  FsFile* file = nullptr;
  TaskHandle_t waiter = nullptr;
  stbi_uc* pixels = nullptr;
  int width = 0;
  int height = 0;
  int components = 0;
  bool ok = false;
};

constexpr uint32_t kStbDecodeTaskStackBytes = 48 * 1024;

void decodeProgressiveJpegTask(void* argument) {
  auto* job = static_cast<ProgressiveDecodeJob*>(argument);
  if (job && job->file) {
    StbiFileContext context{*job->file};
    stbi_io_callbacks callbacks = {stbiRead, stbiSkip, stbiEof};
    job->file->seek(0);
    job->pixels = stbi_load_from_callbacks(&callbacks, &context, &job->width, &job->height, &job->components, 1);
    job->ok = job->pixels != nullptr && job->width > 0 && job->height > 0;
  }
  if (job && job->waiter) {
    xTaskNotifyGive(job->waiter);
  }
  vTaskDelete(nullptr);
}

int stbiRead(void* user, char* data, const int size) {
  auto* context = static_cast<StbiFileContext*>(user);
  if (!context || size <= 0) return 0;
  const int bytesRead = static_cast<int>(context->file.read(reinterpret_cast<uint8_t*>(data), static_cast<size_t>(size)));
  esp_task_wdt_reset();
  yield();
  return bytesRead;
}

void stbiSkip(void* user, const int count) {
  auto* context = static_cast<StbiFileContext*>(user);
  if (!context || count == 0) return;
  const int64_t current = static_cast<int64_t>(context->file.position());
  const int64_t destination = std::max<int64_t>(0, current + count);
  context->file.seek(static_cast<uint32_t>(destination));
}

int stbiEof(void* user) {
  auto* context = static_cast<StbiFileContext*>(user);
  return !context || context->file.position() >= context->file.size();
}

bool renderProgressiveJpegFull(FsFile& file, GfxRenderer& renderer, int x, int y, int targetWidth, int targetHeight,
                               bool cropToFill, ImageRenderMode mode, bool quality, JpegLevelCapture* capture,
                               const float cropAnchorX = 0.5f) {
  ProgressiveDecodeJob job;
  job.file = &file;
  job.waiter = xTaskGetCurrentTaskHandle();
  TaskHandle_t worker = nullptr;
  const BaseType_t created =
      xTaskCreatePinnedToCore(decodeProgressiveJpegTask, "StbDecode", kStbDecodeTaskStackBytes, &job, 0, &worker,
                              ARDUINO_RUNNING_CORE);
  if (created != pdPASS) {
    return false;
  }
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

  stbi_uc* decoded = job.pixels;
  const int sourceWidth = job.width;
  const int sourceHeight = job.height;
  if (!job.ok || !decoded || sourceWidth <= 0 || sourceHeight <= 0) {
    if (decoded) stbi_image_free(decoded);
    return false;
  }

  int outputWidth = sourceWidth;
  int outputHeight = sourceHeight;
  int cropX = 0;
  int cropY = 0;
  int cropWidth = sourceWidth;
  int cropHeight = sourceHeight;
  const float sx = static_cast<float>(targetWidth) / static_cast<float>(sourceWidth);
  const float sy = static_cast<float>(targetHeight) / static_cast<float>(sourceHeight);
  if (cropToFill) {
    const float scale = std::max(sx, sy);
    cropWidth = std::max(1, static_cast<int>(targetWidth / scale));
    cropHeight = std::max(1, static_cast<int>(targetHeight / scale));
    const float anchor = std::max(0.0f, std::min(1.0f, cropAnchorX));
    cropX = std::max(0, static_cast<int>((sourceWidth - cropWidth) * anchor));
    cropY = std::max(0, (sourceHeight - cropHeight) / 2);
    outputWidth = targetWidth;
    outputHeight = targetHeight;
  } else {
    const float scale = std::min(sx, sy);
    outputWidth = std::max(1, static_cast<int>(std::lround(sourceWidth * scale)));
    outputHeight = std::max(1, static_cast<int>(std::lround(sourceHeight * scale)));
  }

  ProgressiveRenderOutput output{renderer,
                                 x + (targetWidth - outputWidth) / 2,
                                 y + (targetHeight - outputHeight) / 2,
                                 mode,
                                 quality,
                                 nullptr,
                                 nullptr,
                                 capture};
  std::unique_ptr<FourToneImageDitherer> twoBitDitherer;
  std::unique_ptr<Atkinson1BitDitherer> oneBitDitherer;
  if (mode == ImageRenderMode::TwoBit) {
    twoBitDitherer.reset(new (std::nothrow) FourToneImageDitherer(outputWidth));
    if (!twoBitDitherer || !twoBitDitherer->ok()) {
      stbi_image_free(decoded);
      return false;
    }
    output.twoBitDitherer = twoBitDitherer.get();
  } else {
    oneBitDitherer.reset(new (std::nothrow) Atkinson1BitDitherer(outputWidth));
    if (!oneBitDitherer) {
      stbi_image_free(decoded);
      return false;
    }
    output.oneBitDitherer = oneBitDitherer.get();
  }

  if (capture && mode == ImageRenderMode::TwoBit) {
    const size_t pixels = static_cast<size_t>(outputWidth) * outputHeight;
    const size_t needed = (pixels + 3) / 4;
    if (needed > 0 && needed <= capture->capacity) {
      capture->width = outputWidth;
      capture->height = outputHeight;
      capture->drawOffsetX = output.drawX;
      capture->drawOffsetY = output.drawY;
      capture->captured = true;
      memset(capture->values, 0, needed);
    } else {
      output.capture = nullptr;
    }
  }

  const bool horizontalUpscale = outputWidth > cropWidth;
  const bool verticalUpscale = outputHeight > cropHeight;
  const uint32_t scaleX_fp =
      static_cast<uint32_t>((static_cast<uint64_t>(cropWidth) << 16) / static_cast<uint32_t>(outputWidth));
  const uint32_t scaleY_fp =
      static_cast<uint32_t>((static_cast<uint64_t>(cropHeight) << 16) / static_cast<uint32_t>(outputHeight));

  std::unique_ptr<uint8_t[]> tempRow(new (std::nothrow) uint8_t[outputWidth]);
  std::unique_ptr<uint8_t[]> cachedRow0(new (std::nothrow) uint8_t[outputWidth]);
  std::unique_ptr<uint8_t[]> cachedRow1(new (std::nothrow) uint8_t[outputWidth]);
  std::unique_ptr<uint8_t[]> outputRow(new (std::nothrow) uint8_t[outputWidth]);
  std::unique_ptr<uint32_t[]> rowAccum(new (std::nothrow) uint32_t[outputWidth]);
  if (!tempRow || !cachedRow0 || !cachedRow1 || !outputRow || !rowAccum) {
    stbi_image_free(decoded);
    return false;
  }

  auto scaleRowX = [&](const uint8_t* srcRow, uint8_t* outRow) {
    for (int ox = 0; ox < outputWidth; ox++) {
      if (horizontalUpscale) {
        const uint32_t srcFp = static_cast<uint32_t>(static_cast<uint64_t>(ox) * scaleX_fp);
        const int sx0 = std::min(static_cast<int>(srcFp >> 16), cropWidth - 1);
        const int sx1 = std::min(sx0 + 1, cropWidth - 1);
        const uint32_t frac = srcFp & 0xFFFFu;
        outRow[ox] = static_cast<uint8_t>((srcRow[sx0] * (0x10000u - frac) + srcRow[sx1] * frac) >> 16);
      } else {
        int sxStart = static_cast<int>((static_cast<uint64_t>(ox) * scaleX_fp) >> 16);
        int sxEnd = static_cast<int>((static_cast<uint64_t>(ox + 1) * scaleX_fp) >> 16);
        sxStart = std::max(0, std::min(cropWidth - 1, sxStart));
        sxEnd = std::max(sxStart + 1, std::min(cropWidth, sxEnd));
        uint32_t sum = 0;
        for (int sx = sxStart; sx < sxEnd; sx++) sum += srcRow[sx];
        outRow[ox] = static_cast<uint8_t>(sum / static_cast<uint32_t>(sxEnd - sxStart));
      }
    }
  };

  int cachedIndex0 = -1;
  int cachedIndex1 = -1;
  auto horizontallyScaledRow = [&](const int srcY) -> uint8_t* {
    if (srcY == cachedIndex0) return cachedRow0.get();
    if (srcY == cachedIndex1) return cachedRow1.get();
    const uint8_t* sourceRow = decoded + static_cast<size_t>(cropY + srcY) * sourceWidth + cropX;
    uint8_t* slot = cachedIndex1 <= cachedIndex0 ? cachedRow1.get() : cachedRow0.get();
    scaleRowX(sourceRow, slot);
    if (slot == cachedRow1.get()) {
      cachedIndex1 = srcY;
    } else {
      cachedIndex0 = srcY;
    }
    return slot;
  };

  bool ok = true;
  for (int oy = 0; oy < outputHeight && ok; ++oy) {
    if ((oy & 15) == 0) {
      esp_task_wdt_reset();
      yield();
    }
    if (verticalUpscale) {
      const uint32_t srcFp = static_cast<uint32_t>(static_cast<uint64_t>(oy) * scaleY_fp);
      const int sy0 = std::min(static_cast<int>(srcFp >> 16), cropHeight - 1);
      const int sy1 = std::min(sy0 + 1, cropHeight - 1);
      const uint32_t frac = srcFp & 0xFFFFu;
      const uint8_t* row0 = horizontallyScaledRow(sy0);
      const uint8_t* row1 = horizontallyScaledRow(sy1);
      if (frac == 0) {
        memcpy(outputRow.get(), row0, static_cast<size_t>(outputWidth));
      } else {
        for (int ox = 0; ox < outputWidth; ox++) {
          outputRow[ox] = static_cast<uint8_t>((row0[ox] * (0x10000u - frac) + row1[ox] * frac) >> 16);
        }
      }
    } else {
      int syStart = static_cast<int>((static_cast<uint64_t>(oy) * scaleY_fp) >> 16);
      int syEnd = static_cast<int>((static_cast<uint64_t>(oy + 1) * scaleY_fp) >> 16);
      syStart = std::max(0, std::min(cropHeight - 1, syStart));
      syEnd = std::max(syStart + 1, std::min(cropHeight, syEnd));
      std::fill(rowAccum.get(), rowAccum.get() + outputWidth, 0u);
      for (int sy = syStart; sy < syEnd; sy++) {
        const uint8_t* sourceRow = decoded + static_cast<size_t>(cropY + sy) * sourceWidth + cropX;
        scaleRowX(sourceRow, tempRow.get());
        for (int ox = 0; ox < outputWidth; ox++) rowAccum[ox] += tempRow[ox];
      }
      const uint32_t count = static_cast<uint32_t>(syEnd - syStart);
      for (int ox = 0; ox < outputWidth; ox++) {
        outputRow[ox] = static_cast<uint8_t>(rowAccum[ox] / count);
      }
    }
    ok = ProgressiveRenderOutput::emit(&output, static_cast<uint16_t>(oy), outputRow.get(),
                                       static_cast<uint16_t>(outputWidth));
  }
  stbi_image_free(decoded);
  return ok;
}

bool renderProgressiveJpegDc(FsFile& file, GfxRenderer* renderer, int x, int y, int targetWidth, int targetHeight,
                             bool cropToFill, ImageRenderMode mode, bool quality, JpegLevelCapture* capture,
                             ProgressiveRowCallback rowCallback = nullptr, void* rowCallbackContext = nullptr,
                             const float cropAnchorX = 0.5f) {
  file.seek(0);
  ProgressiveJpeg jpeg{file};
  auto readBytes = [&](uint8_t* destination, const uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
      const int value = jpeg.rawByte();
      if (value < 0) return false;
      destination[i] = static_cast<uint8_t>(value);
    }
    return true;
  };

  uint8_t soi[2];
  if (!readBytes(soi, 2) || soi[0] != 0xFF || soi[1] != 0xD8) return false;

  auto huffmanTables = std::unique_ptr<ProgressiveHuffman[]>(new (std::nothrow) ProgressiveHuffman[4]);
  if (!huffmanTables) return false;
  uint16_t width = 0;
  uint16_t height = 0;
  uint16_t restartInterval = 0;
  uint16_t dcQuant[4] = {1, 1, 1, 1};
  struct Component {
    uint8_t id = 0;
    uint8_t horizontal = 1;
    uint8_t vertical = 1;
    uint8_t quantTable = 0;
    uint8_t dcTable = 0;
  } components[4];
  uint8_t componentCount = 0;
  uint8_t scanComponents[4] = {};
  uint8_t scanCount = 0;
  uint8_t successiveLow = 0;

  for (;;) {
    int marker = jpeg.rawByte();
    while (marker == 0xFF) marker = jpeg.rawByte();
    if (marker < 0) return false;
    if (marker == 0xD8 || (marker >= 0xD0 && marker <= 0xD7) || marker == 0x01) continue;

    uint8_t lengthBytes[2];
    if (!readBytes(lengthBytes, 2)) return false;
    uint32_t segmentLength = (static_cast<uint32_t>(lengthBytes[0]) << 8) | lengthBytes[1];
    if (segmentLength < 2) return false;
    segmentLength -= 2;

    if (marker == 0xDB) {
      while (segmentLength > 0) {
        uint8_t tableInfo;
        if (!readBytes(&tableInfo, 1)) return false;
        const uint8_t precision = tableInfo >> 4;
        const uint8_t tableIndex = tableInfo & 0x0F;
        const uint32_t tableBytes = precision ? 128 : 64;
        if (tableIndex > 3 || segmentLength < tableBytes + 1) return false;
        uint8_t first[2] = {};
        if (!readBytes(first, precision ? 2 : 1)) return false;
        dcQuant[tableIndex] = precision ? static_cast<uint16_t>((first[0] << 8) | first[1]) : first[0];
        for (uint32_t i = precision ? 2u : 1u; i < tableBytes; ++i) {
          uint8_t ignored;
          if (!readBytes(&ignored, 1)) return false;
        }
        segmentLength -= tableBytes + 1;
      }
    } else if (marker == 0xC4) {
      while (segmentLength > 0) {
        uint8_t tableInfo;
        uint8_t counts[16];
        if (!readBytes(&tableInfo, 1) || !readBytes(counts, sizeof(counts))) return false;
        uint32_t symbolCount = 0;
        for (uint8_t count : counts) symbolCount += count;
        if (symbolCount > 256 || segmentLength < 17 + symbolCount) return false;
        if ((tableInfo >> 4) == 0 && (tableInfo & 0x0F) < 4) {
          ProgressiveHuffman& table = huffmanTables[tableInfo & 0x0F];
          for (int i = 0; i < 16; ++i) table.counts[i + 1] = counts[i];
          if (!readBytes(table.symbols, symbolCount)) return false;
          buildProgressiveHuffman(table);
        } else {
          for (uint32_t i = 0; i < symbolCount; ++i) {
            uint8_t ignored;
            if (!readBytes(&ignored, 1)) return false;
          }
        }
        segmentLength -= 17 + symbolCount;
      }
    } else if (marker == 0xC2) {
      uint8_t sof[6];
      if (!readBytes(sof, sizeof(sof))) return false;
      height = static_cast<uint16_t>((sof[1] << 8) | sof[2]);
      width = static_cast<uint16_t>((sof[3] << 8) | sof[4]);
      componentCount = sof[5];
      if (componentCount == 0 || componentCount > 4 || segmentLength != 6u + componentCount * 3u) return false;
      for (uint8_t i = 0; i < componentCount; ++i) {
        uint8_t component[3];
        if (!readBytes(component, sizeof(component))) return false;
        components[i].id = component[0];
        components[i].horizontal = component[1] >> 4;
        components[i].vertical = component[1] & 0x0F;
        components[i].quantTable = component[2] & 3;
        if (components[i].horizontal == 0 || components[i].vertical == 0) return false;
      }
    } else if (marker == 0xDD) {
      uint8_t restart[2];
      if (segmentLength != 2 || !readBytes(restart, sizeof(restart))) return false;
      restartInterval = static_cast<uint16_t>((restart[0] << 8) | restart[1]);
    } else if (marker == 0xDA) {
      uint8_t count;
      if (!readBytes(&count, 1) || count == 0 || count > 4 || segmentLength != 1u + count * 2u + 3u) return false;
      scanCount = count;
      for (uint8_t i = 0; i < count; ++i) {
        uint8_t scan[2];
        if (!readBytes(scan, sizeof(scan))) return false;
        uint8_t componentIndex = 0xFF;
        for (uint8_t c = 0; c < componentCount; ++c) {
          if (components[c].id == scan[0]) componentIndex = c;
        }
        if (componentIndex == 0xFF) return false;
        components[componentIndex].dcTable = scan[1] >> 4;
        scanComponents[i] = componentIndex;
      }
      uint8_t progressiveParams[3];
      if (!readBytes(progressiveParams, sizeof(progressiveParams))) return false;
      if (progressiveParams[0] != 0 || progressiveParams[1] != 0 || (progressiveParams[2] >> 4) != 0) return false;
      successiveLow = progressiveParams[2] & 0x0F;
      break;
    } else if (marker == 0xC0 || marker == 0xC1) {
      return false;
    } else {
      for (uint32_t i = 0; i < segmentLength; ++i) {
        uint8_t ignored;
        if (!readBytes(&ignored, 1)) return false;
      }
    }
  }

  if (width == 0 || height == 0 || componentCount == 0) return false;
  uint8_t maxHorizontal = 1;
  uint8_t maxVertical = 1;
  for (uint8_t i = 0; i < componentCount; ++i) {
    maxHorizontal = std::max(maxHorizontal, components[i].horizontal);
    maxVertical = std::max(maxVertical, components[i].vertical);
  }
  const Component& luma = components[0];
  if (luma.horizontal != maxHorizontal || luma.vertical != maxVertical) return false;
  const bool interleaved = scanCount > 1;
  if (!interleaved && scanComponents[0] != 0) return false;

  const uint16_t blocksWidth = static_cast<uint16_t>((width + 7) / 8);
  const uint16_t blocksHeight = static_cast<uint16_t>((height + 7) / 8);
  const uint16_t mcusX = static_cast<uint16_t>((width + 8 * maxHorizontal - 1) / (8 * maxHorizontal));
  const uint16_t mcusY = static_cast<uint16_t>((height + 8 * maxVertical - 1) / (8 * maxVertical));
  const uint16_t paddedBlocksWidth = interleaved ? static_cast<uint16_t>(mcusX * luma.horizontal) : blocksWidth;

  int outWidth = width;
  int outHeight = height;
  int cropSourceX = 0;
  int cropSourceY = 0;
  int cropSourceWidth = width;
  int cropSourceHeight = height;
  const float sx = static_cast<float>(targetWidth) / static_cast<float>(width);
  const float sy = static_cast<float>(targetHeight) / static_cast<float>(height);
  if (cropToFill) {
    const float scale = std::max(sx, sy);
    cropSourceWidth = std::max(1, static_cast<int>(targetWidth / scale));
    cropSourceHeight = std::max(1, static_cast<int>(targetHeight / scale));
    const float anchor = std::max(0.0f, std::min(1.0f, cropAnchorX));
    cropSourceX = std::max(0, static_cast<int>((static_cast<int>(width) - cropSourceWidth) * anchor));
    cropSourceY = std::max(0, (static_cast<int>(height) - cropSourceHeight) / 2);
    outWidth = targetWidth;
    outHeight = targetHeight;
  } else {
    const float scale = std::min(sx, sy);
    outWidth = std::max(1, static_cast<int>(std::lround(width * scale)));
    outHeight = std::max(1, static_cast<int>(std::lround(height * scale)));
  }

  const int blockOffsetX = std::min<int>(blocksWidth - 1, cropSourceX / 8);
  const int blockOffsetY = std::min<int>(blocksHeight - 1, cropSourceY / 8);
  const int blockEndX = std::min<int>(blocksWidth, (cropSourceX + cropSourceWidth + 7) / 8);
  const int blockEndY = std::min<int>(blocksHeight, (cropSourceY + cropSourceHeight + 7) / 8);
  const int sourceBlockWidth = std::max(1, blockEndX - blockOffsetX);
  const int sourceBlockHeight = std::max(1, blockEndY - blockOffsetY);
  const int drawX = x + (targetWidth - outWidth) / 2;
  const int drawY = y + (targetHeight - outHeight) / 2;

  std::unique_ptr<ProgressiveRenderOutput> output;
  std::unique_ptr<FourToneImageDitherer> twoBitDitherer;
  std::unique_ptr<Atkinson1BitDitherer> oneBitDitherer;
  if (!rowCallback) {
    if (!renderer) return false;
    output.reset(new (std::nothrow) ProgressiveRenderOutput{*renderer, drawX, drawY, mode, quality, nullptr, nullptr,
                                                             capture});
    if (!output) return false;
    if (mode == ImageRenderMode::TwoBit) {
      twoBitDitherer.reset(new (std::nothrow) FourToneImageDitherer(outWidth));
      if (!twoBitDitherer || !twoBitDitherer->ok()) return false;
      output->twoBitDitherer = twoBitDitherer.get();
    } else {
      oneBitDitherer.reset(new (std::nothrow) Atkinson1BitDitherer(outWidth));
      if (!oneBitDitherer) return false;
      output->oneBitDitherer = oneBitDitherer.get();
    }
  }

  if (!rowCallback && capture && mode == ImageRenderMode::TwoBit) {
    const size_t pixels = static_cast<size_t>(outWidth) * outHeight;
    const size_t needed = (pixels + 3) / 4;
    if (needed > 0 && needed <= capture->capacity) {
      capture->width = outWidth;
      capture->height = outHeight;
      capture->drawOffsetX = drawX;
      capture->drawOffsetY = drawY;
      capture->captured = true;
      memset(capture->values, 0, needed);
    } else {
      output->capture = nullptr;
    }
  }

  ProgressiveResampler resampler;
  resampler.sourceWidth = static_cast<uint16_t>(sourceBlockWidth);
  resampler.sourceHeight = static_cast<uint16_t>(sourceBlockHeight);
  resampler.outputWidth = static_cast<uint16_t>(outWidth);
  resampler.outputHeight = static_cast<uint16_t>(outHeight);
  resampler.previousRow = new (std::nothrow) uint8_t[outWidth];
  resampler.currentRow = new (std::nothrow) uint8_t[outWidth];
  resampler.outputRow = new (std::nothrow) uint8_t[outWidth];
  auto freeResamplerRows = [&] {
    delete[] resampler.previousRow;
    delete[] resampler.currentRow;
    delete[] resampler.outputRow;
    resampler.previousRow = nullptr;
    resampler.currentRow = nullptr;
    resampler.outputRow = nullptr;
  };
  if (!resampler.previousRow || !resampler.currentRow || !resampler.outputRow) {
    freeResamplerRows();
    return false;
  }
  resampler.callback = rowCallback ? rowCallback : ProgressiveRenderOutput::emit;
  resampler.callbackContext = rowCallback ? rowCallbackContext : output.get();
  std::unique_ptr<uint8_t[]> rowBuffer(new (std::nothrow) uint8_t[static_cast<size_t>(paddedBlocksWidth) * luma.vertical]);
  if (!rowBuffer) {
    freeResamplerRows();
    return false;
  }

  int32_t predictors[4] = {0, 0, 0, 0};
  uint32_t blocksSinceRestart = 0;
  const uint16_t lumaQuant = dcQuant[luma.quantTable];
  uint32_t emittedBlockRows = 0;
  const uint32_t rows = interleaved ? mcusY : blocksHeight;
  const uint32_t columns = interleaved ? mcusX : blocksWidth;
  bool decoded = true;
  for (uint32_t row = 0; row < rows && decoded; ++row) {
    memset(rowBuffer.get(), 128, static_cast<size_t>(paddedBlocksWidth) * luma.vertical);
    for (uint32_t column = 0; column < columns && decoded; ++column) {
      if (restartInterval != 0 && blocksSinceRestart == restartInterval) {
        jpeg.bitCount = 0;
        int marker = jpeg.pendingMarker;
        jpeg.pendingMarker = 0;
        if (marker == 0) {
          marker = jpeg.rawByte();
          while (marker == 0xFF) marker = jpeg.rawByte();
        }
        if (marker < 0xD0 || marker > 0xD7) {
          decoded = false;
          break;
        }
        predictors[0] = predictors[1] = predictors[2] = predictors[3] = 0;
        blocksSinceRestart = 0;
      }
      const uint8_t scanComponentCount = interleaved ? scanCount : 1;
      for (uint8_t scanIndex = 0; scanIndex < scanComponentCount && decoded; ++scanIndex) {
        const uint8_t componentIndex = scanComponents[scanIndex];
        const Component& component = components[componentIndex];
        if (!huffmanTables[component.dcTable].ready) {
          decoded = false;
          break;
        }
        const uint8_t horizontal = interleaved ? component.horizontal : 1;
        const uint8_t vertical = interleaved ? component.vertical : 1;
        for (uint8_t by = 0; by < vertical && decoded; ++by) {
          for (uint8_t bx = 0; bx < horizontal; ++bx) {
            const int category = jpeg.decodeHuffman(huffmanTables[component.dcTable]);
            if (category < 0 || category > 15) {
              decoded = false;
              break;
            }
            bool receiveOk = true;
            predictors[componentIndex] += jpeg.receiveExtend(category, receiveOk);
            if (!receiveOk) {
              decoded = false;
              break;
            }
            if (componentIndex == 0) {
              const int32_t dc = (predictors[componentIndex] << successiveLow) * static_cast<int32_t>(lumaQuant);
              const int gray = std::max(0, std::min(255, 128 + static_cast<int>(dc / 8)));
              const uint32_t pixelX = interleaved ? column * luma.horizontal + bx : column;
              const uint32_t pixelY = static_cast<uint32_t>(by) * paddedBlocksWidth;
              if (pixelX < paddedBlocksWidth && pixelY < static_cast<uint32_t>(paddedBlocksWidth) * luma.vertical) {
                rowBuffer[pixelY + pixelX] = static_cast<uint8_t>(gray);
              }
            }
          }
        }
      }
      ++blocksSinceRestart;
    }

    const uint8_t rowsHere = interleaved ? luma.vertical : 1;
    for (uint8_t by = 0; by < rowsHere && emittedBlockRows < blocksHeight && decoded; ++by) {
      if (emittedBlockRows >= static_cast<uint32_t>(blockOffsetY) &&
          emittedBlockRows < static_cast<uint32_t>(blockEndY)) {
        const uint8_t* sourceRow = rowBuffer.get() + static_cast<size_t>(by) * paddedBlocksWidth + blockOffsetX;
        if (!resampler.feed(sourceRow, emittedBlockRows - blockOffsetY)) decoded = false;
      }
      ++emittedBlockRows;
    }
  }
  if (decoded && !resampler.stopped) resampler.finish();
  freeResamplerRows();
  if (!decoded || jpeg.ioError) return false;
  return resampler.nextOutputRow == static_cast<uint32_t>(outHeight);
}

struct ProgressiveThumbnailOutput {
  uint8_t* pixels = nullptr;
  uint16_t width = 0;
  uint16_t height = 0;
};

bool emitProgressiveThumbnailRow(void* context, const uint16_t row, const uint8_t* pixels, const uint16_t width) {
  auto* output = static_cast<ProgressiveThumbnailOutput*>(context);
  if (!output || !output->pixels || row >= output->height || width != output->width) return false;
  uint8_t* destination = output->pixels + static_cast<size_t>(row) * output->width * 3u;
  for (uint16_t x = 0; x < width; ++x) {
    const uint8_t gray = pixels[x];
    destination[x * 3u] = gray;
    destination[x * 3u + 1] = gray;
    destination[x * 3u + 2] = gray;
  }
  return true;
}

Print* gProgressiveThumbnailOut = nullptr;

void writeProgressiveThumbnailByte(unsigned char byte) {
  if (gProgressiveThumbnailOut) gProgressiveThumbnailOut->write(static_cast<uint8_t>(byte));
}

}

bool JpegRender::writeProgressiveThumbnailJpeg(FsFile& jpegFile, Print& jpegOut, const int targetMaxWidth,
                                               const int targetMaxHeight, uint8_t quality) {
  if (!jpegFile || targetMaxWidth <= 0 || targetMaxHeight <= 0) return false;

  int sourceWidth = 0;
  int sourceHeight = 0;
  if (!getDimensions(jpegFile, &sourceWidth, &sourceHeight) || sourceWidth <= 0 || sourceHeight <= 0) return false;

  const float scaleX = static_cast<float>(targetMaxWidth) / static_cast<float>(sourceWidth);
  const float scaleY = static_cast<float>(targetMaxHeight) / static_cast<float>(sourceHeight);
  const float scale = std::min(1.0f, std::min(scaleX, scaleY));
  const int outputWidth = std::max(1, static_cast<int>(std::lround(sourceWidth * scale)));
  const int outputHeight = std::max(1, static_cast<int>(std::lround(sourceHeight * scale)));
  if (outputWidth > 65535 || outputHeight > 65535) return false;

  const size_t outputBytes = static_cast<size_t>(outputWidth) * static_cast<size_t>(outputHeight) * 3u;
  auto* thumbnail = static_cast<uint8_t*>(stbiTransientAlloc(outputBytes));
  if (!thumbnail) return false;

  ProgressiveThumbnailOutput output{thumbnail, static_cast<uint16_t>(outputWidth),
                                    static_cast<uint16_t>(outputHeight)};
  const bool decoded = renderProgressiveJpegDc(jpegFile, nullptr, 0, 0, targetMaxWidth, targetMaxHeight, false,
                                               ImageRenderMode::OneBit, false, nullptr, emitProgressiveThumbnailRow,
                                               &output);
  if (!decoded) {
    stbiTransientFree(thumbnail);
    return false;
  }

  quality = std::max<uint8_t>(1, std::min<uint8_t>(100, quality));
  gProgressiveThumbnailOut = &jpegOut;
  const bool encoded = TooJpeg::writeJpeg(writeProgressiveThumbnailByte, thumbnail,
                                          static_cast<unsigned short>(outputWidth),
                                          static_cast<unsigned short>(outputHeight), true, quality, true);
  gProgressiveThumbnailOut = nullptr;
  stbiTransientFree(thumbnail);
  return encoded;
}

bool JpegRender::render(FsFile& jpegFile, int x, int y, int targetWidth, int targetHeight, bool cropToFill,
                        const ImageRenderMode mode, const bool quality, JpegLevelCapture* capture,
                        const float cropAnchorX) const {
  const uint32_t tRenderStart = millis();
  if (!jpegFile || targetWidth <= 0 || targetHeight <= 0) {
    return false;
  }
  if (isUnsupportedJpeg(jpegFile)) {
    const bool usedFull = renderProgressiveJpegFull(jpegFile, renderer_, x, y, targetWidth, targetHeight, cropToFill,
                                                    mode, quality, capture, cropAnchorX);
    bool ok = usedFull;
    if (!ok) {
      ok = renderProgressiveJpegDc(jpegFile, &renderer_, x, y, targetWidth, targetHeight, cropToFill, mode, quality,
                                   capture, nullptr, nullptr, cropAnchorX);
    }
    EPUB_PERF_LOG("[%lu] [IMG-TIMING] progressive JPEG render=%lums ok=%d full=%d\n", millis(),
                  static_cast<unsigned long>(millis() - tRenderStart), ok ? 1 : 0, usedFull ? 1 : 0);
    return ok;
  }
  const uint32_t tAfterHeaderScan = millis();
  constexpr size_t kJpegDecodeBufferSize = 16 * 1024;
  std::unique_ptr<uint8_t, decltype(&freeJpegIoBuffer)> readBuffer(allocateJpegIoBuffer(kJpegDecodeBufferSize),
                                                                    freeJpegIoBuffer);
  if (!readBuffer) {
    return false;
  }
  JpegReadContext context = {jpegFile, readBuffer.get(), kJpegDecodeBufferSize, 0, 0};
  pjpeg_image_info_t imageInfo;
  PicoJpegDecodeGuard picoJpegGuard;
  if (pjpeg_decode_init(&imageInfo, jpegReadCallback, &context, 0) != 0) {
    return false;
  }
  const uint32_t tAfterInit = millis();

  int outWidth = imageInfo.m_width;
  int outHeight = imageInfo.m_height;
  uint32_t scaleX_fp = 65536;
  uint32_t scaleY_fp = 65536;
  int srcOffsetX = 0;
  int srcOffsetY = 0;
  int cropSrcWidth = imageInfo.m_width;
  int cropSrcHeight = imageInfo.m_height;

  {
    const float sx = static_cast<float>(targetWidth) / static_cast<float>(imageInfo.m_width);
    const float sy = static_cast<float>(targetHeight) / static_cast<float>(imageInfo.m_height);
    if (cropToFill) {
      const float scale = std::max(sx, sy);
      cropSrcWidth = std::max(1, static_cast<int>(targetWidth / scale));
      cropSrcHeight = std::max(1, static_cast<int>(targetHeight / scale));
      const float anchor = std::max(0.0f, std::min(1.0f, cropAnchorX));
      srcOffsetX = std::max(0, static_cast<int>((imageInfo.m_width - cropSrcWidth) * anchor));
      srcOffsetY = std::max(0, (imageInfo.m_height - cropSrcHeight) / 2);
      outWidth = targetWidth;
      outHeight = targetHeight;
    } else {
      float scale = std::min(sx, sy);
      outWidth = std::max(1, static_cast<int>(std::lround(imageInfo.m_width * scale)));
      outHeight = std::max(1, static_cast<int>(std::lround(imageInfo.m_height * scale)));
    }
    scaleX_fp = static_cast<uint32_t>((static_cast<uint64_t>(cropSrcWidth) << 16) / static_cast<uint32_t>(outWidth));
    scaleY_fp = static_cast<uint32_t>((static_cast<uint64_t>(cropSrcHeight) << 16) / static_cast<uint32_t>(outHeight));
  }

  const int drawOffsetX = x + (targetWidth - outWidth) / 2;
  const int drawOffsetY = y + (targetHeight - outHeight) / 2;
  const int srcYEnd = srcOffsetY + cropSrcHeight;
  const bool verticalUpscale = outHeight > cropSrcHeight;
  const bool horizontalUpscale = outWidth > cropSrcWidth;

  const bool captureRequested = capture != nullptr && mode == ImageRenderMode::TwoBit;
  if (captureRequested) {
    const size_t pixelCount = static_cast<size_t>(outWidth) * static_cast<size_t>(outHeight);
    const size_t needed = (pixelCount + 3) / 4;
    if (needed > 0 && needed <= capture->capacity) {
      capture->width = outWidth;
      capture->height = outHeight;
      capture->drawOffsetX = drawOffsetX;
      capture->drawOffsetY = drawOffsetY;
      capture->captured = true;
      memset(capture->values, 0, needed);
    } else {
      capture = nullptr;
    }
  } else {
    capture = nullptr;
  }

  uint8_t* mcuRowBuffer = static_cast<uint8_t*>(malloc(static_cast<size_t>(imageInfo.m_width) * imageInfo.m_MCUHeight));
  uint8_t* scaledRow = static_cast<uint8_t*>(malloc(static_cast<size_t>(outWidth)));
  uint8_t* prevScaledRow = verticalUpscale ? static_cast<uint8_t*>(malloc(static_cast<size_t>(outWidth))) : nullptr;
  uint8_t* blendedRow = verticalUpscale ? static_cast<uint8_t*>(malloc(static_cast<size_t>(outWidth))) : nullptr;
  uint32_t* rowAccum = new (std::nothrow) uint32_t[outWidth]();
  uint16_t* rowCount = new (std::nothrow) uint16_t[outWidth]();
  FourToneImageDitherer* twoBitDitherer = nullptr;
  Atkinson1BitDitherer* oneBitDitherer = nullptr;
  if (mode == ImageRenderMode::TwoBit) {
    twoBitDitherer = new (std::nothrow) FourToneImageDitherer(outWidth);
  } else {
    oneBitDitherer = new (std::nothrow) Atkinson1BitDitherer(outWidth);
  }
  if (!mcuRowBuffer || !scaledRow || (verticalUpscale && (!prevScaledRow || !blendedRow)) || !rowAccum || !rowCount ||
      (mode == ImageRenderMode::TwoBit && (!twoBitDitherer || !twoBitDitherer->ok())) ||
      (mode == ImageRenderMode::OneBit && !oneBitDitherer)) {
    free(mcuRowBuffer);
    free(scaledRow);
    free(prevScaledRow);
    free(blendedRow);
    delete[] rowAccum;
    delete[] rowCount;
    delete twoBitDitherer;
    delete oneBitDitherer;
    return false;
  }

  const bool qualityTone = quality;

  int currentOutY = 0;
  uint32_t nextOutY_srcStart = scaleY_fp;
  bool hasPrevScaledRow = false;

  auto buildScaledRow = [&](const uint8_t* srcRow, uint8_t* row) {
    for (int ox = 0; ox < outWidth; ox++) {
      if (horizontalUpscale) {
        const uint32_t srcFp = static_cast<uint32_t>((static_cast<uint64_t>(ox) * scaleX_fp));
        int sx0 = srcOffsetX + static_cast<int>(srcFp >> 16);
        if (sx0 < srcOffsetX) sx0 = srcOffsetX;
        if (sx0 >= srcOffsetX + cropSrcWidth) sx0 = srcOffsetX + cropSrcWidth - 1;
        const int sx1 = std::min(srcOffsetX + cropSrcWidth - 1, sx0 + 1);
        const uint32_t frac = srcFp & 0xFFFFu;
        const uint32_t invFrac = 65536u - frac;
        row[ox] = static_cast<uint8_t>((srcRow[sx0] * invFrac + srcRow[sx1] * frac + 32768u) >> 16);
      } else {
        int sxStart = srcOffsetX + static_cast<int>((static_cast<uint64_t>(ox) * scaleX_fp) >> 16);
        int sxEnd = srcOffsetX + static_cast<int>((static_cast<uint64_t>(ox + 1) * scaleX_fp) >> 16);
        sxStart = std::max(srcOffsetX, std::min(srcOffsetX + cropSrcWidth - 1, sxStart));
        sxEnd = std::max(sxStart + 1, std::min(srcOffsetX + cropSrcWidth, sxEnd));
        uint32_t sum = 0;
        for (int sx = sxStart; sx < sxEnd; sx++) {
          sum += srcRow[sx];
        }
        row[ox] = static_cast<uint8_t>(sum / static_cast<uint32_t>(sxEnd - sxStart));
      }
    }
  };

  auto blendScaledRows = [&](const uint8_t* upper, const uint8_t* lower, uint32_t frac, uint8_t* row) {
    if (frac == 0) {
      memcpy(row, upper, static_cast<size_t>(outWidth));
      return;
    }
    if (frac >= 65536u) {
      memcpy(row, lower, static_cast<size_t>(outWidth));
      return;
    }
    const uint32_t invFrac = 65536u - frac;
    for (int ox = 0; ox < outWidth; ox++) {
      row[ox] = static_cast<uint8_t>((upper[ox] * invFrac + lower[ox] * frac + 32768u) >> 16);
    }
  };

  ImageServiceBudget imageService;
  auto emitOutputRow = [&](const int screenY, const uint8_t* row) {
    imageService.service();
    for (int step = 0; step < outWidth; step++) {
      const int ox = step;
      const int gray = row[ox];

      int q;
      const int solidBlackMax = mode == ImageRenderMode::TwoBit ? kJpegTwoBitSolidBlackMax : kJpegDitherSolidBlackMax;
      const int solidWhiteMin = mode == ImageRenderMode::TwoBit ? kJpegTwoBitSolidWhiteMin : kJpegDitherSolidWhiteMin;
      if (gray <= solidBlackMax) {
        q = 0;
      } else if (gray >= solidWhiteMin) {
        q = 255;
      } else {
        if (mode == ImageRenderMode::TwoBit) {
          const int leftGray = ox > 0 ? row[ox - 1] : gray;
          const int rightGray = ox + 1 < outWidth ? row[ox + 1] : gray;
          const int tone = jpegTone(gray, leftGray, rightGray, drawOffsetX + ox, screenY, qualityTone);
          q = (qualityTone ? twoBitDitherer->processQuality(tone, step) : twoBitDitherer->process(tone, step)).value;
        } else if (oneBitDitherer) {
          q = oneBitDitherer->processPixel(darkenOneBitJpegGray(gray), step) ? 255 : 0;
        } else {
          q = quantizeGray(darkenOneBitJpegGray(gray), mode);
        }
      }
      if (mode == ImageRenderMode::TwoBit) {
        const uint8_t level = adjustTwoBitImageLevelForDisplay(FourToneImageDitherer::levelFromValue(q));
        if (capture) {
          const size_t pixelIndex = static_cast<size_t>(screenY - drawOffsetY) * outWidth + ox;
          capture->values[pixelIndex / 4] |= static_cast<uint8_t>((level & 0x3) << ((pixelIndex % 4) * 2));
        }
        drawPixelForLevel(renderer_, drawOffsetX + ox, screenY, level);
      } else if (q == 0) {
        renderer_.drawPixel(drawOffsetX + ox, screenY, true);
      }
    }
    if (mode == ImageRenderMode::TwoBit) {
      twoBitDitherer->nextRow();
    } else if (oneBitDitherer) {
      oneBitDitherer->nextRow();
    }
  };

  uint32_t mcuDecodeMs = 0;
  uint32_t rowProcessMs = 0;
  bool decodeComplete = true;
  for (int mcuY = 0; mcuY < imageInfo.m_MCUSPerCol && decodeComplete; mcuY++) {
    const uint32_t tMcuStart = millis();
    for (int mcuX = 0; mcuX < imageInfo.m_MCUSPerRow; mcuX++) {
      imageService.service();
      if (pjpeg_decode_mcu() != 0) {
        decodeComplete = false;
        break;
      }
      for (int bY = 0; bY < imageInfo.m_MCUHeight; bY++) {
        for (int bX = 0; bX < imageInfo.m_MCUWidth; bX++) {
          const int pX = mcuX * imageInfo.m_MCUWidth + bX;
          if (pX >= imageInfo.m_width) continue;
          const int off = (bY / 8 * (imageInfo.m_MCUWidth / 8) + bX / 8) * 64 + (bY % 8) * 8 + (bX % 8);
          uint8_t gray = (imageInfo.m_comps == 1) ? imageInfo.m_pMCUBufR[off]
                                                  : grayFromRgb(imageInfo.m_pMCUBufR[off], imageInfo.m_pMCUBufG[off],
                                                                imageInfo.m_pMCUBufB[off]);
          mcuRowBuffer[bY * imageInfo.m_width + pX] = gray;
        }
      }
    }
    const uint32_t tMcuEnd = millis();
    mcuDecodeMs += tMcuEnd - tMcuStart;

    for (int yInMcu = 0; yInMcu < imageInfo.m_MCUHeight && (mcuY * imageInfo.m_MCUHeight + yInMcu) < imageInfo.m_height;
         yInMcu++) {
      const uint32_t tRowStart = millis();
      const int srcY = mcuY * imageInfo.m_MCUHeight + yInMcu;
      if (srcY < srcOffsetY || srcY >= srcYEnd) continue;
      const uint8_t* srcRow = mcuRowBuffer + yInMcu * imageInfo.m_width;

      buildScaledRow(srcRow, scaledRow);

      if (verticalUpscale) {
        const int cropY = srcY - srcOffsetY;
        if (!hasPrevScaledRow || cropY == 0) {
          while (currentOutY < outHeight) {
            const uint32_t srcFp = static_cast<uint32_t>(static_cast<uint64_t>(currentOutY) * scaleY_fp);
            if ((srcFp >> 16) > 0) {
              break;
            }
            emitOutputRow(drawOffsetY + currentOutY, scaledRow);
            currentOutY++;
          }
        } else {
          const uint32_t prevBase = static_cast<uint32_t>(cropY - 1) << 16;
          const uint32_t currBase = static_cast<uint32_t>(cropY) << 16;
          while (currentOutY < outHeight) {
            const uint32_t srcFp = static_cast<uint32_t>(static_cast<uint64_t>(currentOutY) * scaleY_fp);
            if (srcFp > currBase) {
              break;
            }
            const uint32_t frac = srcFp <= prevBase ? 0u : std::min<uint32_t>(65536u, srcFp - prevBase);
            blendScaledRows(prevScaledRow, scaledRow, frac, blendedRow);
            emitOutputRow(drawOffsetY + currentOutY, blendedRow);
            currentOutY++;
          }
        }
        memcpy(prevScaledRow, scaledRow, static_cast<size_t>(outWidth));
        hasPrevScaledRow = true;
        rowProcessMs += millis() - tRowStart;
        continue;
      }

      for (int ox = 0; ox < outWidth; ox++) {
        rowAccum[ox] += scaledRow[ox];
        rowCount[ox]++;
      }
      bool emittedOutputRow = false;
      const uint32_t cropRowsSeen = static_cast<uint32_t>(srcY - srcOffsetY + 1);
      while ((cropRowsSeen << 16) >= nextOutY_srcStart && currentOutY < outHeight) {
        const int screenY = drawOffsetY + currentOutY;
        for (int ox = 0; ox < outWidth; ox++) {
          scaledRow[ox] = rowCount[ox] ? static_cast<uint8_t>(rowAccum[ox] / rowCount[ox]) : 0;
        }
        emitOutputRow(screenY, scaledRow);
        currentOutY++;
        emittedOutputRow = true;
        nextOutY_srcStart = static_cast<uint32_t>(currentOutY + 1) * scaleY_fp;
      }
      if (emittedOutputRow) {
        memset(rowAccum, 0, static_cast<size_t>(outWidth) * sizeof(uint32_t));
        memset(rowCount, 0, static_cast<size_t>(outWidth) * sizeof(uint16_t));
      }
      rowProcessMs += millis() - tRowStart;
    }
    imageService.service();
  }

  while (verticalUpscale && hasPrevScaledRow && currentOutY < outHeight) {
    emitOutputRow(drawOffsetY + currentOutY, prevScaledRow);
    currentOutY++;
  }

  const bool baselineOk = decodeComplete && currentOutY == outHeight;
  free(mcuRowBuffer);
  free(scaledRow);
  free(prevScaledRow);
  free(blendedRow);
  delete[] rowAccum;
  delete[] rowCount;
  delete twoBitDitherer;
  delete oneBitDitherer;
  const uint32_t tEnd = millis();
  EPUB_PERF_LOG(
      "[%lu] [IMG-TIMING] JPEG %dx%d->%dx%d mode=%d quality=%d capture=%d: headerScan=%lums init=%lums "
      "mcuDecode=%lums rowProcess=%lums decode+draw=%lums total=%lums\n",
      tEnd, imageInfo.m_width, imageInfo.m_height, outWidth, outHeight, static_cast<int>(mode),
      static_cast<int>(quality), capture ? 1 : 0, static_cast<unsigned long>(tAfterHeaderScan - tRenderStart),
      static_cast<unsigned long>(tAfterInit - tAfterHeaderScan), static_cast<unsigned long>(mcuDecodeMs),
      static_cast<unsigned long>(rowProcessMs), static_cast<unsigned long>(tEnd - tAfterInit),
      static_cast<unsigned long>(tEnd - tRenderStart));
  if (baselineOk) {
    return true;
  }

  picoJpegGuard.release();
  jpegFile.seek(0);
  const bool fallbackOk = renderProgressiveJpegFull(jpegFile, renderer_, x, y, targetWidth, targetHeight, cropToFill,
                                                    mode, quality, capture, cropAnchorX);
  EPUB_PERF_LOG("[%lu] [IMG-TIMING] JPEG fallback render=%lums ok=%d\n", millis(),
                static_cast<unsigned long>(millis() - tRenderStart), fallbackOk ? 1 : 0);
  return fallbackOk;
}

bool JpegRender::fromPath(const std::string& path, int x, int y, int targetWidth, int targetHeight, bool cropToFill,
                          const ImageRenderMode mode, const bool quality, JpegLevelCapture* capture,
                          const float cropAnchorX) const {
  const uint32_t tOpenStart = millis();
  FsFile file;
  if (!SdMan.openFileForRead("JRG", path, file)) {
    return false;
  }
  const uint32_t tOpenEnd = millis();
  const bool ok = render(file, x, y, targetWidth, targetHeight, cropToFill, mode, quality, capture, cropAnchorX);
  file.close();
  EPUB_PERF_LOG("[%lu] [IMG-TIMING] fromPath %s: open=%lums render=%lums\n", millis(), path.c_str(),
                static_cast<unsigned long>(tOpenEnd - tOpenStart), static_cast<unsigned long>(millis() - tOpenEnd));
  return ok;
}

void JpegRender::replayCapture(const JpegLevelCapture& capture, const ImageRenderMode mode) const {
  if (!capture.captured || !capture.values) {
    return;
  }
  const uint32_t tStart = millis();
  ImageServiceBudget imageService;
  for (int row = 0; row < capture.height; row++) {
    imageService.service();
    const int screenY = capture.drawOffsetY + row;
    for (int ox = 0; ox < capture.width; ox++) {
      const size_t pixelIndex = static_cast<size_t>(row) * capture.width + ox;
      const uint8_t level = (capture.values[pixelIndex / 4] >> ((pixelIndex % 4) * 2)) & 0x3;
      drawPixelForLevel(renderer_, capture.drawOffsetX + ox, screenY, level);
    }
  }
  EPUB_PERF_LOG("[%lu] [IMG-TIMING] JPEG replay %dx%d: %lums\n", millis(), capture.width, capture.height,
                static_cast<unsigned long>(millis() - tStart));
}

bool JpegRender::getDimensions(FsFile& jpegFile, int* outW, int* outH) {
  if (!outW || !outH || !jpegFile) {
    return false;
  }
  *outW = 0;
  *outH = 0;
  const bool ok = scanJpegHeader(jpegFile, outW, outH, nullptr);
  return ok && *outW > 0 && *outH > 0;
}

bool JpegRender::getDimensions(const std::string& path, int* outW, int* outH) {
  FsFile file;
  if (!SdMan.openFileForRead("JRG", path, file)) {
    return false;
  }
  const bool ok = getDimensions(file, outW, outH);
  file.close();
  return ok;
}

namespace StbImageDecode {

bool render(FsFile& file, GfxRenderer& renderer, int x, int y, int targetWidth, int targetHeight, bool cropToFill,
            ImageRenderMode mode, bool quality, JpegLevelCapture* capture, float cropAnchorX) {
  return renderProgressiveJpegFull(file, renderer, x, y, targetWidth, targetHeight, cropToFill, mode, quality,
                                   capture, cropAnchorX);
}

bool getDimensions(FsFile& file, int* outW, int* outH) {
  StbiFileContext context{file};
  stbi_io_callbacks callbacks = {stbiRead, stbiSkip, stbiEof};
  file.seek(0);
  int comp = 0;
  const int ok = stbi_info_from_callbacks(&callbacks, &context, outW, outH, &comp);
  return ok != 0 && *outW > 0 && *outH > 0;
}

}
