/**
 * @file HalDisplay.cpp
 * @brief Murphy M4 display HAL backed by FreeInkDisplay.
 */

#include <BoardConfig.h>

// Murphy needs one extra byte in SSD1677's Display Update Control 1 register,
// but that is a panel quirk, not a FreeInk SDK behavior. Expose the bus only
// in this HAL translation unit so the workaround remains local to Murphy.
#define private public
#include <EInkDisplay.h>
#undef private

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

constexpr uint8_t kSsd1677DisplayUpdateControl1 = 0x21;
constexpr uint8_t kSsd1677Control1Normal = 0x00;
constexpr uint8_t kSsd1677Control1BypassRed = 0x40;

// Match StickyDisplay's completion guard. BUSY can remain low briefly after
// an async start; finishing immediately can miss the transition and leave the
// controller's previous-frame state stale, which appears as idle ghosting.
void finishMurphyRefresh(EInkDisplay& display) {
  if (!display.isRefreshPending()) return;

  const unsigned long started = millis();
  while (!display.refreshBusy() && millis() - started < 25) delay(1);
  while (display.refreshBusy()) delay(1);
  display.finishDisplayAsync();
}

// The Murphy panel expects the two-byte form of SSD1677 command 0x21. The
// vendored driver writes the first byte as part of every refresh, so priming
// the register here leaves the second byte at the required value without
// adding a Murphy branch to the vendored driver.
void primeMurphyUpdateControl1(EInkDisplay& display, const HalDisplay::RefreshMode mode) {
  finishMurphyRefresh(display);
  const uint8_t firstByte = (mode == HalDisplay::FAST_REFRESH || mode == HalDisplay::MANUAL_REFRESH)
                                ? kSsd1677Control1Normal
                                : kSsd1677Control1BypassRed;
  display._bus.cmdData2(kSsd1677DisplayUpdateControl1, firstByte, 0x00);
}

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
              panelPins().busy),
      windowedGray(display) {}

HalDisplay::~HalDisplay() = default;

void HalDisplay::begin() {
  display.setBusyWaitSliceHook(murphyBusyWaitSlice);
  display.begin();
  primeMurphyUpdateControl1(display, FAST_REFRESH);

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
  finishMurphyRefresh(const_cast<EInkDisplay&>(display));
}

void HalDisplay::drawImage(const uint8_t* imageData, const uint16_t x, const uint16_t y, const uint16_t w,
                           const uint16_t h, const bool fromProgmem) const {
  display.drawImage(imageData, x, y, w, h, fromProgmem);
}

void HalDisplay::displayBuffer(const HalDisplay::RefreshMode mode) {
  primeMurphyUpdateControl1(display, mode);
  display.displayBuffer(convertRefreshMode(mode));
}

void HalDisplay::displayWindow(const uint16_t x, const uint16_t y, const uint16_t w, const uint16_t h,
                               const HalDisplay::RefreshMode mode) {
  primeMurphyUpdateControl1(display, mode);
  display.displayWindow(x, y, w, h);
#ifndef EINK_DISPLAY_SINGLE_BUFFER_MODE
  if (display.frameBuffer != nullptr && display.frameBufferActive != nullptr)
    memcpy(display.frameBufferActive, display.frameBuffer, display.getBufferSize());
#endif
}

void HalDisplay::displayBufferAsync(const HalDisplay::RefreshMode mode) {
  primeMurphyUpdateControl1(display, mode);
  display.displayBufferAsync(convertRefreshMode(mode));
}

void HalDisplay::displayGrayscaleBase(const HalDisplay::RefreshMode mode, const bool turnOffScreen) {
  primeMurphyUpdateControl1(display, mode);
  display.displayGrayscaleBase(convertRefreshMode(mode), turnOffScreen);
}

void HalDisplay::syncWriteBufferFromActive() const { display.syncWriteBufferFromActive(); }

bool HalDisplay::refreshBusy() { return display.refreshBusy(); }

void HalDisplay::refreshDisplay(const HalDisplay::RefreshMode mode, const bool turnOffScreen) {
  primeMurphyUpdateControl1(display, mode);
  display.refreshDisplay(convertRefreshMode(mode), turnOffScreen);
}

void HalDisplay::deepSleep() {
  finishMurphyRefresh(display);
  display.deepSleep();
}

uint8_t* HalDisplay::getFrameBuffer() const { return display.getFrameBuffer(); }

void HalDisplay::copyGrayscaleBuffers(const uint8_t* lsbBuffer, const uint8_t* msbBuffer) {
  finishMurphyRefresh(display);
  if (windowedGray.isArmed()) {
    windowedGray.copyLsb(lsbBuffer);
    windowedGray.copyMsb(msbBuffer);
  } else {
    display.copyGrayscaleBuffers(lsbBuffer, msbBuffer);
  }
}

void HalDisplay::copyGrayscaleLsbBuffers(const uint8_t* lsbBuffer) {
  finishMurphyRefresh(display);
  if (windowedGray.isArmed()) windowedGray.copyLsb(lsbBuffer);
  else display.copyGrayscaleLsbBuffers(lsbBuffer);
}

void HalDisplay::copyGrayscaleMsbBuffers(const uint8_t* msbBuffer) {
  finishMurphyRefresh(display);
  if (windowedGray.isArmed()) windowedGray.copyMsb(msbBuffer);
  else display.copyGrayscaleMsbBuffers(msbBuffer);
}

void HalDisplay::cleanupGrayscaleBuffers(const uint8_t* bwBuffer) {
  finishMurphyRefresh(display);
  display.cleanupGrayscaleBuffers(bwBuffer);
}

void HalDisplay::displayGrayBuffer(const bool quality, const bool trackForRevert) {
  (void)trackForRevert;
  primeMurphyUpdateControl1(display, FAST_REFRESH);
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
  primeMurphyUpdateControl1(display, FAST_REFRESH);
  display.displayGrayBuffer(false, kMurphyQualityFastLut, true);
}

void HalDisplay::displayGrayBufferWindow(const uint16_t x, const uint16_t y, const uint16_t w, const uint16_t h,
                                         const bool quality, const bool fastQuality) {
  primeMurphyUpdateControl1(display, FAST_REFRESH);
  if (quality) {
    if (!windowedGray.displayWindow(x, y, w, h,
                                    fastQuality ? kMurphyQualityFastLut : freeink::lut_factory_quality))
      INX_SERIAL.printf("[MURPHY_M4][GRAY-WINDOW] rejected x=%u y=%u w=%u h=%u; full refresh suppressed\n", x, y, w,
                        h);
    return;
  }
  display.displayGrayBuffer(false, nullptr, false);
}

bool HalDisplay::supportsWindowedQuality() const { return windowedGray.supports(); }

void HalDisplay::prepareQualityGrayscale() {
  finishMurphyRefresh(display);
  display.prepareGrayscaleTarget();
}

void HalDisplay::prepareQualityGrayscaleWindow(const uint16_t x, const uint16_t y, const uint16_t w,
                                               const uint16_t h) {
  prepareQualityGrayscale();
  windowedGray.prepare(x, y, w, h);
}

void HalDisplay::forceGrayWaveformVariant(const uint8_t variant) {
  (void)variant;
}

uint16_t HalDisplay::getDisplayWidth() const { return display.getDisplayWidth(); }

uint16_t HalDisplay::getDisplayHeight() const { return display.getDisplayHeight(); }

uint16_t HalDisplay::getDisplayWidthBytes() const { return display.getDisplayWidthBytes(); }

uint32_t HalDisplay::getBufferSize() const { return display.getBufferSize(); }
