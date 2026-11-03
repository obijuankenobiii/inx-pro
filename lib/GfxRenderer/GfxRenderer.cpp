/**
 * @file GfxRenderer.cpp
 * @brief Definitions for GfxRenderer.
 */

#include "GfxRenderer.h"

#include <Utf8.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <vector>

#if defined(ARDUINO_ARCH_ESP32)
#include <esp_heap_caps.h>
#endif

#include "../../src/system/ScreenComponents.h"

namespace {

uint8_t* allocateBwSnapshotChunk(const size_t bytes) {
#if defined(ARDUINO_ARCH_ESP32)
  if (void* memory = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)) {
    return static_cast<uint8_t*>(memory);
  }
  return static_cast<uint8_t*>(heap_caps_malloc(bytes, MALLOC_CAP_8BIT));
#else
  return static_cast<uint8_t*>(std::malloc(bytes));
#endif
}

void freeBwSnapshotChunk(void* memory) {
  if (!memory) return;
#if defined(ARDUINO_ARCH_ESP32)
  heap_caps_free(memory);
#else
  std::free(memory);
#endif
}

}

GfxRenderer::GfxRenderer(HalDisplay& halDisplay)
    : display(halDisplay),
      renderMode(BW),
      orientation(Portrait),
      rectangle(*this),
      line(*this),
      circle(*this),
      icon(*this),
      polygon(*this),
      bitmap(*this),
      text(*this),
      ui(*this) {}

GfxRenderer::~GfxRenderer() { freeBwBufferChunks(); }

void GfxRenderer::begin() {
  panelWidth = display.getDisplayWidth();
  panelHeight = display.getDisplayHeight();
  panelWidthBytes = display.getDisplayWidthBytes();
  frameBufferSize = display.getBufferSize();
  bwBufferChunks.assign((frameBufferSize + BW_BUFFER_CHUNK_SIZE - 1) / BW_BUFFER_CHUNK_SIZE, nullptr);
}

void GfxRenderer::insertFont(const int fontId, EpdFontFamily font) {
  auto it =
      std::find_if(fontSlots.begin(), fontSlots.end(), [fontId](const FontSlot& slot) { return slot.id == fontId; });
  if (it != fontSlots.end()) {
    it->family = std::move(font);
    return;
  }
  fontSlots.emplace_back(fontId, std::move(font));
}

const EpdFontFamily* GfxRenderer::findFontFamily(const int fontId) const {
  const auto it =
      std::find_if(fontSlots.begin(), fontSlots.end(), [fontId](const FontSlot& slot) { return slot.id == fontId; });
  return it == fontSlots.end() ? nullptr : &it->family;
}

ExternalFont* GfxRenderer::findStreamingFont(const EpdFontData* data) const {
  if (!data) {
    return nullptr;
  }
  const auto it = std::find_if(streamingFontSlots.begin(), streamingFontSlots.end(),
                               [data](const StreamingFontSlot& slot) { return slot.data == data; });
  return it == streamingFontSlots.end() ? nullptr : it->stream.get();
}

__attribute__((optimize("O2"))) void GfxRenderer::rotateCoordinates(const int x, const int y, int* rotatedX,
                                                                    int* rotatedY) const {
  switch (orientation) {
    case Portrait: {
      *rotatedX = y;
      *rotatedY = panelHeight - 1 - x;
      break;
    }
    case LandscapeClockwise: {
      *rotatedX = panelWidth - 1 - x;
      *rotatedY = panelHeight - 1 - y;
      break;
    }
    case PortraitInverted: {
      *rotatedX = panelWidth - 1 - y;
      *rotatedY = x;
      break;
    }
    case LandscapeCounterClockwise: {
      *rotatedX = x;
      *rotatedY = y;
      break;
    }
  }
}

bool GfxRenderer::mapDisplayRegion(const DisplayRegion& region, uint16_t* x, uint16_t* y, uint16_t* width,
                                   uint16_t* height) const {
  if (!x || !y || !width || !height || region.width <= 0 || region.height <= 0) return false;

  const int left = std::max(0, region.x);
  const int top = std::max(0, region.y);
  const int right = std::min(getScreenWidth(), region.x + region.width);
  const int bottom = std::min(getScreenHeight(), region.y + region.height);
  if (left >= right || top >= bottom) return false;

  const int logicalX[4] = {left, right - 1, left, right - 1};
  const int logicalY[4] = {top, top, bottom - 1, bottom - 1};
  int minX = panelWidth;
  int minY = panelHeight;
  int maxX = -1;
  int maxY = -1;
  for (size_t i = 0; i < 4; ++i) {
    int physicalX = 0;
    int physicalY = 0;
    rotateCoordinates(logicalX[i], logicalY[i], &physicalX, &physicalY);
    minX = std::min(minX, physicalX);
    minY = std::min(minY, physicalY);
    maxX = std::max(maxX, physicalX);
    maxY = std::max(maxY, physicalY);
  }

  minX = std::max(0, minX) & ~7;
  minY = std::max(0, minY);
  maxX = std::min(panelWidth - 1, maxX);
  maxY = std::min(panelHeight - 1, maxY);
  if (minX > maxX || minY > maxY) return false;

  const int endX = std::min(static_cast<int>(panelWidth), (maxX + 8) & ~7);
  *x = static_cast<uint16_t>(minX);
  *y = static_cast<uint16_t>(minY);
  *width = static_cast<uint16_t>(endX - minX);
  *height = static_cast<uint16_t>(maxY - minY + 1);
  return *width != 0 && *height != 0;
}

__attribute__((optimize("O2"))) void GfxRenderer::drawPixel(const int x, const int y, const bool state) const {
  uint8_t* frameBuffer = display.getFrameBuffer();

  if (!frameBuffer) {
    INX_SERIAL.printf("[%lu] [GFX] !! No framebuffer\n", millis());
    return;
  }

  if (x < 0 || x >= getScreenWidth() || y < 0 || y >= getScreenHeight()) {
    return;
  }

  int rotatedX = 0;
  int rotatedY = 0;
  rotateCoordinates(x, y, &rotatedX, &rotatedY);

  if (rotatedX < 0 || rotatedX >= panelWidth || rotatedY < 0 || rotatedY >= panelHeight) {
    return;
  }

  const uint32_t byteIndex = rotatedY * panelWidthBytes + (rotatedX / 8);
  const uint8_t bitPosition = 7 - (rotatedX % 8);

  const bool outputState = darkMode && !imageRendering ? !state : state;
  if (outputState) {
    frameBuffer[byteIndex] &= ~(1 << bitPosition);
  } else {
    frameBuffer[byteIndex] |= 1 << bitPosition;
  }
}

bool GfxRenderer::readPixel(const int x, const int y) const {
  const uint8_t* frameBuffer = display.getFrameBuffer();
  if (!frameBuffer) {
    return false;
  }

  int rotatedX = 0;
  int rotatedY = 0;
  rotateCoordinates(x, y, &rotatedX, &rotatedY);
  if (rotatedX < 0 || rotatedX >= panelWidth || rotatedY < 0 || rotatedY >= panelHeight) {
    return false;
  }

  const uint32_t byteIndex = rotatedY * panelWidthBytes + (rotatedX / 8);
  const uint8_t bitPosition = 7 - (rotatedX % 8);
  return (frameBuffer[byteIndex] & (1 << bitPosition)) == 0;
}

bool GfxRenderer::readPackedRow1bpp(const int x, const int y, const int width, uint8_t* outRow) const {
  if (!outRow || width <= 0) {
    return false;
  }
  const int rowBytes = (width + 7) / 8;
  memset(outRow, 0, static_cast<size_t>(rowBytes));

  const uint8_t* frameBuffer = display.getFrameBuffer();
  if (!frameBuffer) {
    return false;
  }

  for (int col = 0; col < width; col++) {
    int rotatedX = 0;
    int rotatedY = 0;
    rotateCoordinates(x + col, y, &rotatedX, &rotatedY);
    if (rotatedX < 0 || rotatedX >= panelWidth || rotatedY < 0 || rotatedY >= panelHeight) {
      continue;
    }
    const uint32_t byteIndex = rotatedY * panelWidthBytes + (rotatedX / 8);
    const uint8_t bitPosition = 7 - (rotatedX % 8);
    if ((frameBuffer[byteIndex] & (1 << bitPosition)) == 0) {
      outRow[col / 8] |= static_cast<uint8_t>(0x80 >> (col % 8));
    }
  }
  return true;
}

void GfxRenderer::drawPackedRow1bpp(const int x, const int y, const int width, const uint8_t* row) const {
  if (!row || width <= 0) {
    return;
  }
  uint8_t* frameBuffer = display.getFrameBuffer();
  if (!frameBuffer) {
    return;
  }

  // Image rows are already packed.  On the unrotated landscape path, keep
  // them packed all the way into the framebuffer instead of touching every
  // pixel through drawPixel().
  if (orientation == LandscapeCounterClockwise && x >= 0 && y >= 0 && x % 8 == 0 && width % 8 == 0 &&
      x + width <= panelWidth && y < panelHeight) {
    uint8_t* destination = frameBuffer + static_cast<uint32_t>(y) * panelWidthBytes + x / 8;
    const bool invert = darkMode && !imageRendering;
    const int rowBytes = width / 8;
    for (int byte = 0; byte < rowBytes; ++byte) {
      const uint8_t inkBits = row[byte];
      destination[byte] = invert ? inkBits : static_cast<uint8_t>(~inkBits);
    }
    return;
  }

  for (int col = 0; col < width; col++) {
    int rotatedX;
    int rotatedY;
    switch (orientation) {
      case Portrait:
        rotatedX = y;
        rotatedY = panelHeight - 1 - (x + col);
        break;
      case LandscapeClockwise:
        rotatedX = panelWidth - 1 - (x + col);
        rotatedY = panelHeight - 1 - y;
        break;
      case PortraitInverted:
        rotatedX = panelWidth - 1 - y;
        rotatedY = x + col;
        break;
      case LandscapeCounterClockwise:
      default:
        rotatedX = x + col;
        rotatedY = y;
        break;
    }
    if (rotatedX < 0 || rotatedX >= panelWidth || rotatedY < 0 || rotatedY >= panelHeight) {
      continue;
    }
    const bool ink = (row[col / 8] & (0x80 >> (col % 8))) != 0;
    const uint32_t byteIndex = rotatedY * panelWidthBytes + (rotatedX / 8);
    const uint8_t bitPosition = 7 - (rotatedX % 8);
    const bool outputInk = darkMode && !imageRendering ? !ink : ink;
    if (outputInk) {
      frameBuffer[byteIndex] &= static_cast<uint8_t>(~(1 << bitPosition));
    } else {
      frameBuffer[byteIndex] |= static_cast<uint8_t>(1 << bitPosition);
    }
  }
}

void GfxRenderer::clearScreen(const uint8_t color) const {
  ScreenComponents::resetPageHeaderBackButton();
  const uint8_t outputColor = darkMode && !imageRendering && renderMode == BW ? static_cast<uint8_t>(~color) : color;
  display.clearScreen(outputColor);
}

void GfxRenderer::invertScreen() const {
  uint8_t* buffer = display.getFrameBuffer();
  if (!buffer) {
    INX_SERIAL.printf("[%lu] [GFX] !! No framebuffer in invertScreen\n", millis());
    return;
  }
  for (uint32_t i = 0; i < frameBufferSize; i++) {
    buffer[i] = ~buffer[i];
  }
}

void GfxRenderer::invertRect(const DisplayRegion& region) const {
  uint8_t* buffer = display.getFrameBuffer();
  if (!buffer || region.width <= 0 || region.height <= 0) return;

  const int left = std::max(0, region.x);
  const int top = std::max(0, region.y);
  const int right = std::min(getScreenWidth(), region.x + region.width);
  const int bottom = std::min(getScreenHeight(), region.y + region.height);
  if (left >= right || top >= bottom) return;

  for (int logicalY = top; logicalY < bottom; ++logicalY) {
    for (int logicalX = left; logicalX < right; ++logicalX) {
      int physicalX = 0;
      int physicalY = 0;
      rotateCoordinates(logicalX, logicalY, &physicalX, &physicalY);
      if (physicalX < 0 || physicalX >= panelWidth || physicalY < 0 || physicalY >= panelHeight) continue;
      const uint32_t byteIndex = static_cast<uint32_t>(physicalY) * panelWidthBytes + (physicalX / 8);
      buffer[byteIndex] ^= static_cast<uint8_t>(1u << (7 - (physicalX % 8)));
    }
  }
}

void GfxRenderer::displayBuffer(const HalDisplay::RefreshMode refreshMode) const {
  display.displayBuffer(refreshMode);
}

void GfxRenderer::displayWindow(const DisplayRegion& region, const HalDisplay::RefreshMode refreshMode) const {
  uint16_t x = 0;
  uint16_t y = 0;
  uint16_t width = 0;
  uint16_t height = 0;
  if (mapDisplayRegion(region, &x, &y, &width, &height)) {
    display.displayWindow(x, y, width, height, refreshMode);
  }
}

void GfxRenderer::displayBufferAsync(const HalDisplay::RefreshMode refreshMode) const {
  display.displayBufferAsync(refreshMode);
}

bool GfxRenderer::isRefreshBusy() const { return display.refreshBusy(); }

void GfxRenderer::syncWriteBufferFromActive() const { display.syncWriteBufferFromActive(); }

void GfxRenderer::clearRect(const DisplayRegion& region, const uint8_t color) const {
  uint8_t* frameBuffer = display.getFrameBuffer();
  if (!frameBuffer || region.width <= 0 || region.height <= 0) return;

  const bool ink = (color & 0x80) == 0;
  uint16_t physicalX = 0;
  uint16_t physicalY = 0;
  uint16_t physicalWidth = 0;
  uint16_t physicalHeight = 0;
  if (!mapDisplayRegion(region, &physicalX, &physicalY, &physicalWidth, &physicalHeight)) return;

  // The PTL window expands the physical X range to complete source bytes.
  // Clear that same expanded range so its padding cannot refresh stale black
  // pixels around the image.
  for (uint16_t row = physicalY; row < physicalY + physicalHeight; ++row) {
    for (uint16_t column = physicalX; column < physicalX + physicalWidth; ++column) {
      const uint32_t byteIndex = static_cast<uint32_t>(row) * panelWidthBytes + (column / 8);
      const uint8_t bit = static_cast<uint8_t>(1u << (7 - (column % 8)));
      if (ink) {
        frameBuffer[byteIndex] &= static_cast<uint8_t>(~bit);
      } else {
        frameBuffer[byteIndex] |= bit;
      }
    }
  }
}

int GfxRenderer::getScreenWidth() const {
  switch (orientation) {
    case Portrait:
    case PortraitInverted:

      return panelHeight;
    case LandscapeClockwise:
    case LandscapeCounterClockwise:

      return panelWidth;
  }
  return panelHeight;
}

int GfxRenderer::getScreenHeight() const {
  switch (orientation) {
    case Portrait:
    case PortraitInverted:

      return panelWidth;
    case LandscapeClockwise:
    case LandscapeCounterClockwise:

      return panelHeight;
  }
  return panelWidth;
}

uint8_t* GfxRenderer::getFrameBuffer() const { return display.getFrameBuffer(); }

size_t GfxRenderer::getBufferSize() const { return frameBufferSize; }

void GfxRenderer::copyGrayscaleLsbBuffers() const { display.copyGrayscaleLsbBuffers(display.getFrameBuffer()); }

void GfxRenderer::copyGrayscaleMsbBuffers() const { display.copyGrayscaleMsbBuffers(display.getFrameBuffer()); }

void GfxRenderer::displayGrayBuffer(const bool quality, const bool trackForRevert) const {
#ifdef SIMULATOR
  (void)trackForRevert;
  display.displayGrayBuffer(quality);
#else
  display.displayGrayBuffer(quality, trackForRevert);
#endif
}

void GfxRenderer::displayGrayBufferFastQuality() const {
#ifdef SIMULATOR
  display.displayGrayBuffer(false);
#else
  display.displayGrayBufferFastQuality();
#endif
}

void GfxRenderer::displayGrayBufferWindow(const DisplayRegion& region, const bool quality,
                                          const bool fastQuality) const {
  uint16_t x = 0;
  uint16_t y = 0;
  uint16_t width = 0;
  uint16_t height = 0;
  if (mapDisplayRegion(region, &x, &y, &width, &height)) {
    const bool portrait = orientation == Portrait || orientation == PortraitInverted;
    const int expectedPhysicalWidth = portrait ? region.height : region.width;
    const int expectedPhysicalHeight = portrait ? region.width : region.height;
    INX_SERIAL.printf(
        "[%lu] [HQ-WINDOW] logical=%d,%d %dx%d physical=%u,%u %ux%u expected=%dx%d delta=%d,%d orient=%d\n",
        millis(), region.x, region.y, region.width, region.height, x, y, width, height, expectedPhysicalWidth,
        expectedPhysicalHeight, static_cast<int>(width) - expectedPhysicalWidth,
        static_cast<int>(height) - expectedPhysicalHeight, static_cast<int>(orientation));
    display.displayGrayBufferWindow(x, y, width, height, quality, fastQuality);
  }
}

bool GfxRenderer::supportsWindowedQuality() const { return display.supportsWindowedQuality(); }

void GfxRenderer::displayGrayscaleBase(const HalDisplay::RefreshMode fallback, const bool turnOffScreen) const {
  display.displayGrayscaleBase(fallback, turnOffScreen);
}

void GfxRenderer::prepareQualityGrayscale() const {
#ifdef SIMULATOR
  display.preconditionGrayscale();
#else
  display.prepareQualityGrayscale();
#endif
}

void GfxRenderer::prepareQualityGrayscaleWindow(const DisplayRegion& region) const {
#ifdef SIMULATOR
  (void)region;
  display.preconditionGrayscale();
#else
  uint16_t x = 0;
  uint16_t y = 0;
  uint16_t width = 0;
  uint16_t height = 0;
  if (mapDisplayRegion(region, &x, &y, &width, &height)) {
    display.prepareQualityGrayscaleWindow(x, y, width, height);
  } else {
    display.prepareQualityGrayscale();
  }
#endif
}

bool GfxRenderer::copyStoredBwToFramebuffer() const {
  for (const auto& chunk : bwBufferChunks) {
    if (!chunk) return false;
  }
  if (bwBufferChunks.empty()) return false;
  uint8_t* frameBuffer = display.getFrameBuffer();
  if (!frameBuffer) return false;
  for (size_t i = 0; i < bwBufferChunks.size(); i++) {
    const size_t offset = i * BW_BUFFER_CHUNK_SIZE;
    const size_t chunkSize = std::min(BW_BUFFER_CHUNK_SIZE, static_cast<size_t>(frameBufferSize) - offset);
    memcpy(frameBuffer + offset, bwBufferChunks[i], chunkSize);
  }
  return true;
}

void GfxRenderer::freeBwBufferChunks() {
  for (auto& bwBufferChunk : bwBufferChunks) {
    if (bwBufferChunk) {
      freeBwSnapshotChunk(bwBufferChunk);
      bwBufferChunk = nullptr;
    }
  }
}

/**
 * This should be called before grayscale buffers are populated.
 * A `restoreBwBuffer` call should always follow the grayscale render if this method was called.
 * Uses chunked allocation to avoid needing 48KB of contiguous memory.
 * Returns true if buffer was stored successfully, false if allocation failed.
 */
bool GfxRenderer::storeBwBuffer() {
  const uint8_t* frameBuffer = display.getFrameBuffer();
  if (!frameBuffer) {
    INX_SERIAL.printf("[%lu] [GFX] !! No framebuffer in storeBwBuffer\n", millis());
    return false;
  }

  if (bwBufferChunks.empty()) {
    bwBufferChunks.assign((frameBufferSize + BW_BUFFER_CHUNK_SIZE - 1) / BW_BUFFER_CHUNK_SIZE, nullptr);
  }

  for (size_t i = 0; i < bwBufferChunks.size(); i++) {
    if (bwBufferChunks[i]) {
      INX_SERIAL.printf("[%lu] [GFX] !! BW buffer chunk %zu already stored - this is likely a bug, freeing chunk\n",
                    millis(), i);
      freeBwSnapshotChunk(bwBufferChunks[i]);
      bwBufferChunks[i] = nullptr;
    }

    const size_t offset = i * BW_BUFFER_CHUNK_SIZE;
    const size_t chunkSize = std::min(BW_BUFFER_CHUNK_SIZE, static_cast<size_t>(frameBufferSize) - offset);
    bwBufferChunks[i] = allocateBwSnapshotChunk(chunkSize);

    if (!bwBufferChunks[i]) {
      INX_SERIAL.printf("[%lu] [GFX] !! Failed to allocate BW buffer chunk %zu (%zu bytes)\n", millis(), i, chunkSize);

      freeBwBufferChunks();
      return false;
    }

    memcpy(bwBufferChunks[i], frameBuffer + offset, chunkSize);
  }

  return true;
}

/**
 * This can only be called if `storeBwBuffer` was called prior to the grayscale render.
 * It should be called to restore the BW buffer state after grayscale rendering is complete.
 * Uses chunked restoration to match chunked storage.
 */
void GfxRenderer::restoreBwBuffer() {
  bool missingChunks = false;
  for (const auto& bwBufferChunk : bwBufferChunks) {
    if (!bwBufferChunk) {
      missingChunks = true;
      break;
    }
  }

  if (missingChunks) {
    freeBwBufferChunks();
    return;
  }

  uint8_t* frameBuffer = display.getFrameBuffer();
  if (!frameBuffer) {
    INX_SERIAL.printf("[%lu] [GFX] !! No framebuffer in restoreBwBuffer\n", millis());
    freeBwBufferChunks();
    return;
  }

  for (size_t i = 0; i < bwBufferChunks.size(); i++) {
    if (!bwBufferChunks[i]) {
      INX_SERIAL.printf("[%lu] [GFX] !! BW buffer chunks not stored - this is likely a bug\n", millis());
      freeBwBufferChunks();
      return;
    }

    const size_t offset = i * BW_BUFFER_CHUNK_SIZE;
    const size_t chunkSize = std::min(BW_BUFFER_CHUNK_SIZE, static_cast<size_t>(frameBufferSize) - offset);
    memcpy(frameBuffer + offset, bwBufferChunks[i], chunkSize);
  }

  display.cleanupGrayscaleBuffers(frameBuffer);

  freeBwBufferChunks();
}

/**
 * Cleanup grayscale buffers using the current frame buffer.
 * Use this when BW buffer was re-rendered instead of stored/restored.
 */
void GfxRenderer::cleanupGrayscaleWithFrameBuffer() const {
  uint8_t* frameBuffer = display.getFrameBuffer();
  if (frameBuffer) {
    display.cleanupGrayscaleBuffers(frameBuffer);
  }
}

void GfxRenderer::renderGrayscalePasses(const bool quality, const bool preserveText,
                                        const std::function<void()>& drawPlane, const bool fastQuality,
                                        const DisplayRegion* window) {
  const bool useFastQuality = quality && fastQuality;
  const bool useWindow = quality && window != nullptr && supportsWindowedQuality();

  if (quality) {
    if (useWindow) prepareQualityGrayscaleWindow(*window);
    else prepareQualityGrayscale();
  }

  setRenderMode(quality ? GRAY2_LSB : GRAYSCALE_LSB);
  drawPlane();
  copyGrayscaleLsbBuffers();

  setRenderMode(quality ? GRAY2_MSB : GRAYSCALE_MSB);
  drawPlane();
  copyGrayscaleMsbBuffers();

  if (useWindow) {
    displayGrayBufferWindow(*window, true, useFastQuality);
  } else if (useFastQuality) {
    displayGrayBufferFastQuality();
  } else {
    displayGrayBuffer(quality, !preserveText);
  }
  setRenderMode(BW);

  if (preserveText) {
    restoreBwBuffer();
  } else {
    if (useWindow) {
      // The panel still contains the previous frame outside the requested
      // window. Rebuild the host B/W baseline from that committed frame.
      syncWriteBufferFromActive();
    } else {
      clearScreen(0xFF);
    }
    cleanupGrayscaleWithFrameBuffer();
  }
}

void GfxRenderer::resetTransientReaderState() {
  freeBwBufferChunks();
  renderMode = BW;
  // Dual-buffer panels swap on every refresh, so getFrameBuffer() here is the WRITE
  // buffer - a stale frame, not what the panel is showing. Seeding the driver's OLD
  // plane from it leaves the next refresh diffing against garbage. Pull the displayed
  // frame in first.
  syncWriteBufferFromActive();
  cleanupGrayscaleWithFrameBuffer();
}

void GfxRenderer::getOrientedViewableTRBL(int* outTop, int* outRight, int* outBottom, int* outLeft) const {
  switch (orientation) {
    case Portrait:
      *outTop = VIEWABLE_MARGIN_TOP;
      *outRight = VIEWABLE_MARGIN_RIGHT;
      *outBottom = VIEWABLE_MARGIN_BOTTOM;
      *outLeft = VIEWABLE_MARGIN_LEFT;
      break;
    case LandscapeClockwise:
      *outTop = VIEWABLE_MARGIN_LEFT;
      *outRight = VIEWABLE_MARGIN_TOP;
      *outBottom = VIEWABLE_MARGIN_RIGHT;
      *outLeft = VIEWABLE_MARGIN_BOTTOM;
      break;
    case PortraitInverted:
      *outTop = VIEWABLE_MARGIN_BOTTOM;
      *outRight = VIEWABLE_MARGIN_LEFT;
      *outBottom = VIEWABLE_MARGIN_TOP;
      *outLeft = VIEWABLE_MARGIN_RIGHT;
      break;
    case LandscapeCounterClockwise:
      *outTop = VIEWABLE_MARGIN_RIGHT;
      *outRight = VIEWABLE_MARGIN_BOTTOM;
      *outBottom = VIEWABLE_MARGIN_LEFT;
      *outLeft = VIEWABLE_MARGIN_TOP;
      break;
  }
}

void GfxRenderer::insertStreamingFont(int fontId, std::unique_ptr<ExternalFont> streamingFont,
                                      const EpdFontFamily& font) {
  const EpdFontData* dataPtr = streamingFont->getData();
  EpdFontFamily streamingFamily = font;
  streamingFamily.setData(EpdFontFamily::REGULAR, dataPtr);
  auto streamIt = std::find_if(streamingFontSlots.begin(), streamingFontSlots.end(),
                               [dataPtr](const StreamingFontSlot& slot) { return slot.data == dataPtr; });
  if (streamIt != streamingFontSlots.end()) {
    streamIt->stream = std::move(streamingFont);
  } else {
    streamingFontSlots.emplace_back(dataPtr, std::move(streamingFont));
  }
  insertFont(fontId, streamingFamily);
}

void GfxRenderer::removeFont(int fontId) {
  auto it =
      std::find_if(fontSlots.begin(), fontSlots.end(), [fontId](const FontSlot& slot) { return slot.id == fontId; });
  if (it == fontSlots.end()) {
    return;
  }
  for (const auto style :
       {EpdFontFamily::REGULAR, EpdFontFamily::BOLD, EpdFontFamily::ITALIC, EpdFontFamily::BOLD_ITALIC}) {
    const EpdFontData* d = it->family.getData(style);
    if (d != nullptr) {
      streamingFontSlots.erase(std::remove_if(streamingFontSlots.begin(), streamingFontSlots.end(),
                                              [d](const StreamingFontSlot& slot) { return slot.data == d; }),
                               streamingFontSlots.end());
    }
  }
  fontSlots.erase(it);
}

void GfxRenderer::removeAllStreamingFonts() {
  if (streamingFontSlots.empty()) {
    return;
  }

  for (auto it = fontSlots.begin(); it != fontSlots.end();) {
    bool usesStreamingData = false;
    for (const auto style :
         {EpdFontFamily::REGULAR, EpdFontFamily::BOLD, EpdFontFamily::ITALIC, EpdFontFamily::BOLD_ITALIC}) {
      const EpdFontData* data = it->family.getData(style);
      const auto streamIt = std::find_if(streamingFontSlots.begin(), streamingFontSlots.end(),
                                         [data](const StreamingFontSlot& slot) { return slot.data == data; });
      if (data != nullptr && streamIt != streamingFontSlots.end()) {
        usesStreamingData = true;
        break;
      }
    }

    if (usesStreamingData) {
      it = fontSlots.erase(it);
    } else {
      ++it;
    }
  }

  streamingFontSlots.clear();
}

void GfxRenderer::addStreamingFontStyle(int fontId, EpdFontFamily::Style style,
                                        std::unique_ptr<ExternalFont> streamingFont) {
  auto it =
      std::find_if(fontSlots.begin(), fontSlots.end(), [fontId](const FontSlot& slot) { return slot.id == fontId; });
  if (it == fontSlots.end()) {
    INX_SERIAL.printf("[GFX] Can't add style to unknown font ID %d\n", fontId);
    return;
  }

  const EpdFontData* dataPtr = streamingFont->getData();
  auto streamIt = std::find_if(streamingFontSlots.begin(), streamingFontSlots.end(),
                               [dataPtr](const StreamingFontSlot& slot) { return slot.data == dataPtr; });
  if (streamIt != streamingFontSlots.end()) {
    streamIt->stream = std::move(streamingFont);
  } else {
    streamingFontSlots.emplace_back(dataPtr, std::move(streamingFont));
  }

  EpdFontFamily updatedFamily = it->family;
  updatedFamily.setData(style, dataPtr);
  it->family = updatedFamily;
}
