/**
 * @file HalDisplay.cpp
 * @brief LilyGo T5 S3 HalDisplay — thin passthrough to freeink-sdk's FreeInkDisplay.
 *
 * See HalDisplay.h for how this panel differs from the SPI controllers on Sticky and
 * the X4 Pro.
 */

#include <BoardConfig.h>
#include <HalDisplay.h>
#include <HalGPIO.h>

namespace {

const BoardConfig::DisplayPins& panelPins() { return BoardConfig::ACTIVE.display; }

// LgfxEpdDriver maps FULL and HALF to LovyanGFX's epd_text waveform and everything else
// to epd_fast. Keep the app's meaning: FULL/HALF/MANUAL are the ghost-clearing refreshes,
// FAST and STRONG_FAST are page turns.
EInkDisplay::RefreshMode convertRefreshMode(const HalDisplay::RefreshMode mode) {
  switch (mode) {
    case HalDisplay::FULL_REFRESH:
      return EInkDisplay::FULL_REFRESH;
    case HalDisplay::HALF_REFRESH:
    case HalDisplay::MANUAL_REFRESH:
      return EInkDisplay::HALF_REFRESH;
    case HalDisplay::STRONG_FAST_REFRESH:
    case HalDisplay::FAST_REFRESH:
    default:
      return EInkDisplay::FAST_REFRESH;
  }
}

}  // namespace

// The display pins in LILYGO_T5S3 are all PIN_UNASSIGNED: the parallel bus lives in the
// board's LgfxEpdConfig, and the driver reports usesExternalBus() so EpdBus stays idle.
HalDisplay::HalDisplay()
    : display(panelPins().sclk, panelPins().mosi, panelPins().cs, panelPins().dc, panelPins().rst, panelPins().busy) {}

HalDisplay::~HalDisplay() = default;

void HalDisplay::begin() {
  display.begin();
  INX_SERIAL.printf("[T5S3][DISPLAY] LgfxEpd %ux%u\n", display.getDisplayWidth(), display.getDisplayHeight());

  const auto wakeupReason = gpio.getWakeupReason();
  if (wakeupReason == HalGPIO::WakeupReason::PowerButton || wakeupReason == HalGPIO::WakeupReason::AfterFlash ||
      wakeupReason == HalGPIO::WakeupReason::Other) {
    display.requestResync();
  }
}

void HalDisplay::clearScreen(const uint8_t color) const { display.clearScreen(color); }

void HalDisplay::drawImage(const uint8_t* imageData, const uint16_t x, const uint16_t y, const uint16_t w,
                           const uint16_t h, const bool fromProgmem) const {
  display.drawImage(imageData, x, y, w, h, fromProgmem);
}

void HalDisplay::displayBuffer(const HalDisplay::RefreshMode mode) { display.displayBuffer(convertRefreshMode(mode)); }

void HalDisplay::displayBufferAsync(const HalDisplay::RefreshMode mode) {
  display.displayBufferAsync(convertRefreshMode(mode));
}

void HalDisplay::displayGrayscaleBase(const HalDisplay::RefreshMode mode, const bool turnOffScreen) {
  display.displayGrayscaleBase(convertRefreshMode(mode), turnOffScreen);
}

void HalDisplay::syncWriteBufferFromActive() const { display.syncWriteBufferFromActive(); }

bool HalDisplay::refreshBusy() { return display.refreshBusy(); }

void HalDisplay::refreshDisplay(const HalDisplay::RefreshMode mode, const bool turnOffScreen) {
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

// The driver has one gray path: overlay the planes on the base canvas and push with the
// base's waveform. The quality flag only changed which tones went into the planes.
void HalDisplay::displayGrayBuffer(const bool quality, const bool trackForRevert) {
  (void)quality;
  (void)trackForRevert;
  display.displayGrayBuffer(false, nullptr, false);
}

void HalDisplay::displayGrayBufferFastQuality() { display.displayGrayBuffer(false, nullptr, false); }

// Nothing to precondition: there is no controller RAM or LUT state to seed.
void HalDisplay::prepareQualityGrayscale() {}

uint16_t HalDisplay::getDisplayWidth() const { return display.getDisplayWidth(); }

uint16_t HalDisplay::getDisplayHeight() const { return display.getDisplayHeight(); }

uint16_t HalDisplay::getDisplayWidthBytes() const { return display.getDisplayWidthBytes(); }

uint32_t HalDisplay::getBufferSize() const { return display.getBufferSize(); }
