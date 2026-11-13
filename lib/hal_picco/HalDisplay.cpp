#include "HalDisplay.h"

void HalDisplay::begin() { display.begin(); }

void HalDisplay::clearScreen(const uint8_t color) const { display.clearScreen(color); }

void HalDisplay::drawImage(const uint8_t* imageData, const uint16_t x, const uint16_t y, const uint16_t w,
                           const uint16_t h, const bool fromProgmem) const {
  display.drawImage(imageData, x, y, w, h, fromProgmem);
}

namespace {
PiccoDisplay::RefreshMode convertMode(const HalDisplay::RefreshMode mode) {
  switch (mode) {
    case HalDisplay::FULL_REFRESH: return PiccoDisplay::RefreshMode::FULL_REFRESH;
    case HalDisplay::HALF_REFRESH:
    case HalDisplay::MANUAL_REFRESH: return PiccoDisplay::RefreshMode::HALF_REFRESH;
    case HalDisplay::STRONG_FAST_REFRESH:
    case HalDisplay::FAST_REFRESH:
    default: return PiccoDisplay::RefreshMode::FAST_REFRESH;
  }
}
}

void HalDisplay::displayBuffer(const RefreshMode mode) {
  if (mode == MANUAL_REFRESH) {
    display.manualRefresh();
    return;
  }
  display.displayBuffer(convertMode(mode));
}

void HalDisplay::displayWindow(const uint16_t x, const uint16_t y, const uint16_t w, const uint16_t h,
                               const RefreshMode) {
  display.displayWindow(x, y, w, h);
}

void HalDisplay::displayBufferAsync(const RefreshMode mode) { display.displayBufferAsync(convertMode(mode)); }

bool HalDisplay::refreshBusy() { return display.refreshBusy(); }

void HalDisplay::refreshDisplay(const RefreshMode mode, const bool turnOffScreen) {
  display.refreshDisplay(convertMode(mode), turnOffScreen);
}

void HalDisplay::syncWriteBufferFromActive() const { display.syncWriteBufferFromActive(); }
void HalDisplay::deepSleep() { display.deepSleep(); }
uint8_t* HalDisplay::getFrameBuffer() const { return display.getFrameBuffer(); }
void HalDisplay::copyGrayscaleBuffers(const uint8_t* lsbBuffer, const uint8_t* msbBuffer) {
  display.copyGrayscaleBuffers(lsbBuffer, msbBuffer);
}
void HalDisplay::copyGrayscaleLsbBuffers(const uint8_t* lsbBuffer) { display.copyGrayscaleLsbBuffers(lsbBuffer); }
void HalDisplay::copyGrayscaleMsbBuffers(const uint8_t* msbBuffer) { display.copyGrayscaleMsbBuffers(msbBuffer); }
void HalDisplay::cleanupGrayscaleBuffers(const uint8_t* bwBuffer) { display.cleanupGrayscaleBuffers(bwBuffer); }
void HalDisplay::displayGrayBuffer(const bool quality, const bool) { display.displayGrayBuffer(quality); }
void HalDisplay::displayGrayBufferFastQuality() { display.displayGrayBufferFastQuality(); }
void HalDisplay::displayGrayBufferWindow(const uint16_t x, const uint16_t y, const uint16_t w, const uint16_t h,
                                         const bool quality, const bool fastQuality) {
  display.displayGrayBufferWindow(x, y, w, h, quality, fastQuality);
}
bool HalDisplay::supportsWindowedQuality() const { return display.supportsWindowedGray(); }
void HalDisplay::displayGrayscaleBase(const RefreshMode fallback, const bool turnOffScreen) {
  display.displayGrayscaleBase(convertMode(fallback), turnOffScreen);
}
void HalDisplay::prepareQualityGrayscale() { display.prepareQualityGrayscale(); }
void HalDisplay::prepareQualityGrayscaleWindow(const uint16_t x, const uint16_t y, const uint16_t w,
                                               const uint16_t h) {
  display.prepareGrayscaleWindow(x, y, w, h);
}
uint16_t HalDisplay::getDisplayWidth() const { return display.getDisplayWidth(); }
uint16_t HalDisplay::getDisplayHeight() const { return display.getDisplayHeight(); }
uint16_t HalDisplay::getDisplayWidthBytes() const { return display.getDisplayWidthBytes(); }
uint32_t HalDisplay::getBufferSize() const { return display.getBufferSize(); }
