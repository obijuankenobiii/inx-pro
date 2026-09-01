/**
 * @file HalDisplay.cpp
 * @brief Sticky variant of HalDisplay — delegates to freeink-sdk's FreeInkDisplay.
 */

#include <BoardConfig.h>
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <SPI.h>

namespace {
const BoardConfig::DisplayPins& stickyDisplayPins() { return BoardConfig::ACTIVE.display; }
}

HalDisplay::HalDisplay()
    : display(stickyDisplayPins().sclk, stickyDisplayPins().mosi, stickyDisplayPins().cs, stickyDisplayPins().dc,
              stickyDisplayPins().rst, stickyDisplayPins().busy) {}

HalDisplay::~HalDisplay() {}

void HalDisplay::begin() {
  SPI.begin(stickyDisplayPins().sclk, BoardConfig::ACTIVE.sd.miso, stickyDisplayPins().mosi, -1);
  display.begin();

  const auto wakeupReason = gpio.getWakeupReason();
  if (wakeupReason == HalGPIO::WakeupReason::PowerButton || wakeupReason == HalGPIO::WakeupReason::AfterFlash ||
      wakeupReason == HalGPIO::WakeupReason::Other) {
    display.requestResync();
  }
}

void HalDisplay::clearScreen(uint8_t color) const { display.clearScreen(color); }

void HalDisplay::drawImage(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                           bool fromProgmem) const {
  display.drawImage(imageData, x, y, w, h, fromProgmem);
}

StickyDisplay::RefreshMode convertRefreshMode(HalDisplay::RefreshMode mode) {
  switch (mode) {
    case HalDisplay::FULL_REFRESH:
      return EInkDisplay::FULL_REFRESH;
    case HalDisplay::HALF_REFRESH:
      return EInkDisplay::HALF_REFRESH;
    case HalDisplay::MANUAL_REFRESH:
      return EInkDisplay::HALF_REFRESH;
    case HalDisplay::STRONG_FAST_REFRESH:
      return EInkDisplay::FAST_REFRESH;
    case HalDisplay::FAST_REFRESH:
    default:
      return EInkDisplay::FAST_REFRESH;
  }
}

void HalDisplay::displayBuffer(HalDisplay::RefreshMode mode) { display.displayBuffer(convertRefreshMode(mode)); }

void HalDisplay::displayBufferAsync(HalDisplay::RefreshMode mode) {
  display.displayBufferAsync(convertRefreshMode(mode));
}

void HalDisplay::syncWriteBufferFromActive() const { display.syncWriteBufferFromActive(); }

bool HalDisplay::refreshBusy() { return display.refreshBusy(); }

void HalDisplay::refreshDisplay(HalDisplay::RefreshMode mode, bool turnOffScreen) {
  display.refreshDisplay(convertRefreshMode(mode), turnOffScreen);
}

void HalDisplay::deepSleep() { display.deepSleep(); }

uint8_t* HalDisplay::getFrameBuffer() const { return display.getFrameBuffer(); }

void HalDisplay::copyGrayscaleBuffers(const uint8_t* lsbBuffer, const uint8_t* msbBuffer) {
  display.copyGrayscaleBuffers(lsbBuffer, msbBuffer);
}

void HalDisplay::copyGrayscaleLsbBuffers(const uint8_t* lsbBuffer) { display.copyGrayscaleLsbBuffers(lsbBuffer); }

void HalDisplay::copyGrayscaleMsbBuffers(const uint8_t* msbBuffer) { display.copyGrayscaleMsbBuffers(msbBuffer); }

void HalDisplay::cleanupGrayscaleBuffers(const uint8_t* bwBuffer) { display.cleanupGrayscaleBuffers(bwBuffer); }

void HalDisplay::displayGrayBuffer(const bool quality, const bool trackForRevert) {
  (void)trackForRevert;
  display.displayGrayBuffer(quality);
}

void HalDisplay::displayGrayBufferFastQuality() { display.displayGrayBufferFastQuality(); }

void HalDisplay::prepareQualityGrayscale() { display.prepareQualityGrayscale(); }

uint16_t HalDisplay::getDisplayWidth() const { return display.getDisplayWidth(); }

uint16_t HalDisplay::getDisplayHeight() const { return display.getDisplayHeight(); }

uint16_t HalDisplay::getDisplayWidthBytes() const { return display.getDisplayWidthBytes(); }

uint32_t HalDisplay::getBufferSize() const { return display.getBufferSize(); }
