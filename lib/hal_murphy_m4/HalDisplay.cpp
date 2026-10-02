/**
 * @file HalDisplay.cpp
 * @brief Murphy M4 display HAL backed by FreeInkDisplay.
 */

#include <BoardConfig.h>
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <esp_idf_version.h>

#include "../../freeink-sdk/libs/display/FreeInkDisplay/src/lut/Ssd1677Luts.h"

namespace {

// Murphy uses the same SSD1677 four-level image waveform that was already
// validated on the X4 Pro. Keep the data local to this HAL: the Murphy target
// must not link or depend on the X4 Pro display adapter.
inline const unsigned char kMurphyQualityFastLut[] PROGMEM = {
    0x00, 0x4A, 0x88, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x80, 0x62, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x88, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xA8, 0x44, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

    0x05, 0x07, 0x01, 0x02, 0x00,
    0x07, 0x01, 0x04, 0x01, 0x00,
    0x01, 0x00, 0x01, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x01,

    0x22, 0x22, 0x22, 0x22, 0x22,
    0x17, 0x41, 0xA8, 0x32, 0x30};

}  // namespace

namespace {
const BoardConfig::DisplayPins& panelPins() { return BoardConfig::ACTIVE.display; }

bool murphyBusyWaitSlice(const int8_t busyPin, const uint8_t busyLevel) {
  (void)busyPin;
  (void)busyLevel;
  delay(1);
  return true;
}

EInkDisplay::RefreshMode convertRefreshMode(const HalDisplay::RefreshMode mode) {
  switch (mode) {
    case HalDisplay::FULL_REFRESH:
      return EInkDisplay::HALF_REFRESH;
    case HalDisplay::HALF_REFRESH:
      return EInkDisplay::HALF_REFRESH;
    case HalDisplay::MANUAL_REFRESH:
      return EInkDisplay::FAST_REFRESH;
    case HalDisplay::STRONG_FAST_REFRESH:
    case HalDisplay::FAST_REFRESH:
    default:
      return EInkDisplay::FAST_REFRESH;
  }
}
}  // namespace

HalDisplay::HalDisplay()
    : display(panelPins().sclk, panelPins().mosi, panelPins().cs, panelPins().dc, panelPins().rst,
              panelPins().busy) {}

HalDisplay::~HalDisplay() = default;

void HalDisplay::begin() {
  display.setBusyWaitSliceHook(murphyBusyWaitSlice);
  display.begin();

  INX_SERIAL.printf("[MURPHY_M4][DISPLAY] controller=SSD1677 variant=%u dual_buffer=1\n",
                    BoardConfig::ACTIVE.displayControllerVariant);

  const auto wakeupReason = gpio.getWakeupReason();
  if (wakeupReason == HalGPIO::WakeupReason::PowerButton || wakeupReason == HalGPIO::WakeupReason::AfterFlash ||
      wakeupReason == HalGPIO::WakeupReason::Other) {
    display.requestResync();
  }
}

void HalDisplay::clearScreen(const uint8_t color) const { display.clearScreen(color); }

void HalDisplay::clearBothBuffers(const uint8_t color) const {
  display.clearScreen(color);
  finishPendingRefresh();
  syncWriteBufferFromActive();
}

void HalDisplay::finishPendingRefresh() const {
  const_cast<HalDisplay*>(this)->display.waitRefreshComplete();
}

void HalDisplay::drawImage(const uint8_t* imageData, const uint16_t x, const uint16_t y, const uint16_t w,
                           const uint16_t h, const bool fromProgmem) const {
  display.drawImage(imageData, x, y, w, h, fromProgmem);
}

void HalDisplay::displayBuffer(const HalDisplay::RefreshMode mode) {
  display.displayBuffer(convertRefreshMode(mode));
}

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

void HalDisplay::copyGrayscaleLsbBuffers(const uint8_t* lsbBuffer) {
  display.copyGrayscaleLsbBuffers(lsbBuffer);
}

void HalDisplay::copyGrayscaleMsbBuffers(const uint8_t* msbBuffer) {
  display.copyGrayscaleMsbBuffers(msbBuffer);
}

void HalDisplay::cleanupGrayscaleBuffers(const uint8_t* bwBuffer) {
  display.cleanupGrayscaleBuffers(bwBuffer);
}

void HalDisplay::displayGrayBuffer(const bool quality, const bool trackForRevert) {
  (void)trackForRevert;
  if (quality) {
    // Quality images use the absolute four-level waveform. The SDK's default
    // differential grayscale path is intended for text AA and produces broken
    // image tones when fed the two absolute image planes.
    display.displayGrayBuffer(false, freeink::lut_factory_quality, true);
    return;
  }
  display.displayGrayBuffer(false, nullptr, false);
}

void HalDisplay::displayGrayBufferFastQuality() {
  display.displayGrayBuffer(false, kMurphyQualityFastLut, true);
}

void HalDisplay::prepareQualityGrayscale() { display.prepareGrayscaleTarget(); }

void HalDisplay::forceGrayWaveformVariant(const uint8_t variant) {
  (void)variant;
}

uint16_t HalDisplay::getDisplayWidth() const { return display.getDisplayWidth(); }

uint16_t HalDisplay::getDisplayHeight() const { return display.getDisplayHeight(); }

uint16_t HalDisplay::getDisplayWidthBytes() const { return display.getDisplayWidthBytes(); }

uint32_t HalDisplay::getBufferSize() const { return display.getBufferSize(); }
