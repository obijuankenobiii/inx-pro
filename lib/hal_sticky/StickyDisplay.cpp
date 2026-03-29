#include <Arduino.h>
#include <functional>
#include <memory>
#include <vector>

// Sticky owns the reference SSD1677 waveform. This translation unit needs to
// invalidate FreeInk's private shadow flags after that external activation;
// keep the compatibility access local to the board wrapper rather than adding
// a board-specific API to the vendored SDK.
#define private public
#include <StickyDisplay.h>
#undef private

#include "../../freeink-sdk/libs/display/FreeInkDisplay/src/lut/Ssd1677Luts.h"

namespace {
// X4 source-compatible fast quality LUT. Keep this in the board wrapper so
// the Sticky quality path does not depend on the vendored driver waveform.
const unsigned char lut_x4_quality_fast[] PROGMEM = {
    0x00, 0x4A, 0x88, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x62, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x88, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xA8, 0x44, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x09, 0x0C, 0x03, 0x03, 0x00, 0x0F, 0x03,
    0x07, 0x03, 0x00, 0x03, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x44, 0x44, 0x44, 0x44, 0x44, 0x17, 0x41, 0xA8, 0x32, 0x50};

// Corrected X4 quality LUT from the old EInkDisplay driver. The vendored
// FreeInk factory LUT still contains the pre-fix 0x08/0x0B timing and 0x22/
// 0x30 voltage tail, which collapses or misorders Sticky's mid-grays.
const unsigned char lut_x4_quality[] PROGMEM = {
    0x00, 0x4A, 0x88, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x80, 0x62, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x88, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xA8, 0x44, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x09, 0x0C, 0x03, 0x03, 0x00,
    0x0F, 0x03, 0x07, 0x03, 0x00,
    0x03, 0x00, 0x02, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,
    0x44, 0x44, 0x44, 0x44, 0x44,
    0x17, 0x41, 0xA8, 0x32, 0x50};

constexpr uint8_t CMD_DISPLAY_UPDATE_CTRL1 = 0x21;
constexpr uint8_t CMD_DISPLAY_UPDATE_CTRL2 = 0x22;
constexpr uint8_t CMD_MASTER_ACTIVATION = 0x20;
constexpr uint8_t CTRL1_NORMAL = 0x00;
constexpr uint8_t CTRL1_BYPASS_RED = 0x40;
constexpr uint8_t QUALITY_ACTIVATION = 0xC7;
constexpr uint8_t QUALITY_PREPARE_POWERDOWN = 0x03;

void loadReferenceQualityLut(freeink::EpdBus& bus, const unsigned char* lut) {
  bus.cmd(0x32);
  for (uint16_t i = 0; i < 110; ++i) {
    bus.data(pgm_read_byte(&lut[i]));
  }

  bus.cmd(0x03);
  bus.data(pgm_read_byte(&lut[105]));
  bus.cmd(0x04);
  bus.data(pgm_read_byte(&lut[106]));
  bus.data(pgm_read_byte(&lut[107]));
  bus.data(pgm_read_byte(&lut[108]));
  bus.cmd(0x2C);
  bus.data(pgm_read_byte(&lut[109]));
}

void markExternalQualityComplete(EInkDisplay& display) {
  display._shadowValid = false;
  display._redRamSynced = false;
  display.setCustomLUT(false);
}

// Arduino-ESP32 3.3.x/ESP-IDF 5.5 routes attachInterrupt() through the core-0
// IPC task. On Sticky that path can overflow the small ipc0 stack while the
// display BUSY edge is being armed. Keep the SDK's normal refresh timing but
// use its polling wait; this is only a completion-wait policy and does not
// change the panel waveform or framebuffer path.
bool stickyBusyWaitSlice(const int8_t busyPin, const uint8_t busyLevel) {
  (void)busyPin;
  (void)busyLevel;
  delay(1);
  return true;
}

}  // namespace

StickyDisplay::StickyDisplay(const int8_t sclk, const int8_t mosi, const int8_t cs, const int8_t dc,
                             const int8_t rst, const int8_t busy)
    : display(sclk, mosi, cs, dc, rst, busy) {}

void StickyDisplay::begin() {
  // A non-null slice hook makes EpdBus::waitRefreshComplete() use its bounded
  // polling implementation instead of registering a BUSY GPIO ISR.
  display.setBusyWaitSliceHook(stickyBusyWaitSlice);
  display.begin();
  qualityReferenceScreenOn = false;
}

void StickyDisplay::requestResync() { display.requestResync(); }

void StickyDisplay::clearScreen(const uint8_t color) const { display.clearScreen(color); }

void StickyDisplay::drawImage(const uint8_t* imageData, const uint16_t x, const uint16_t y, const uint16_t w,
                              const uint16_t h, const bool fromProgmem) const {
  display.drawImage(imageData, x, y, w, h, fromProgmem);
}

void StickyDisplay::finish() {
  if (!display.isRefreshPending()) return;

  // The Sticky BUSY completion edge can happen before FreeInk arms its ISR
  // waiter. First give BUSY time to assert, then poll it through completion.
  // FreeInk only clears its pending state after the panel is already idle.
  const unsigned long started = millis();
  while (!display.refreshBusy() && millis() - started < 25) delay(1);
  while (display.refreshBusy()) delay(1);
  display.finishDisplayAsync();
}

void StickyDisplay::displayBuffer(const RefreshMode mode) {
  INX_SERIAL.printf("[%lu] [STICKY-DISPLAY] displayBuffer start mode=%d\n", millis(), static_cast<int>(mode));
  finish();
  INX_SERIAL.printf("[%lu] [STICKY-DISPLAY] displayBuffer after finish-before\n", millis());
  display.displayBufferAsync(mode);
  INX_SERIAL.printf("[%lu] [STICKY-DISPLAY] displayBuffer after async\n", millis());
  finish();
  INX_SERIAL.printf("[%lu] [STICKY-DISPLAY] displayBuffer complete\n", millis());
  // Sticky's vendor B/W waveforms (0xFF/0xF7) power the panel down after the
  // refresh. Match EInkDisplay.cpp's isScreenOn guard: preparation is only
  // valid while the preceding B/W activation left the panel powered.
  qualityReferenceScreenOn = false;
}

void StickyDisplay::displayBufferAsync(const RefreshMode mode) {
  finish();
  display.displayBufferAsync(mode);
  qualityReferenceScreenOn = false;
}

void StickyDisplay::refreshDisplay(const RefreshMode mode, const bool turnOffScreen) {
  if (!turnOffScreen) {
    displayBuffer(mode);
    return;
  }

  finish();
  display.triggerDisplayAsync(mode, true);
  finish();
  qualityReferenceScreenOn = false;
}

void StickyDisplay::syncWriteBufferFromActive() const { display.syncWriteBufferFromActive(); }

void StickyDisplay::deepSleep() {
  finish();
  display.deepSleep();
}

uint8_t* StickyDisplay::getFrameBuffer() const { return display.getFrameBuffer(); }

void StickyDisplay::copyGrayscaleBuffers(const uint8_t* lsbBuffer, const uint8_t* msbBuffer) {
  finish();
  display.copyGrayscaleBuffers(lsbBuffer, msbBuffer);
}

void StickyDisplay::copyGrayscaleLsbBuffers(const uint8_t* lsbBuffer) {
  finish();
  display.copyGrayscaleLsbBuffers(lsbBuffer);
}

void StickyDisplay::copyGrayscaleMsbBuffers(const uint8_t* msbBuffer) {
  finish();
  display.copyGrayscaleMsbBuffers(msbBuffer);
}

void StickyDisplay::cleanupGrayscaleBuffers(const uint8_t* bwBuffer) {
  finish();
  display.cleanupGrayscaleBuffers(bwBuffer);
}

void StickyDisplay::displayGrayBuffer(const bool quality) {
  finish();
  if (!quality) {
    display.displayGrayBuffer(false, nullptr, false);
    return;
  }

  freeink::EpdBus& bus = display._bus;
  loadReferenceQualityLut(bus, lut_x4_quality);
  delay(120);
  bus.cmd(CMD_DISPLAY_UPDATE_CTRL1);
  bus.data(CTRL1_NORMAL);
  bus.cmd(CMD_DISPLAY_UPDATE_CTRL2);
  bus.data(QUALITY_ACTIVATION);
  bus.cmd(CMD_MASTER_ACTIVATION);
  bus.waitBusy("sticky_quality_gray");
  markExternalQualityComplete(display);
  qualityReferenceScreenOn = false;
}

void StickyDisplay::displayGrayBufferFastQuality() {
  finish();
  freeink::EpdBus& bus = display._bus;
  loadReferenceQualityLut(bus, lut_x4_quality_fast);
  bus.cmd(CMD_DISPLAY_UPDATE_CTRL1);
  bus.data(CTRL1_NORMAL);
  bus.cmd(CMD_DISPLAY_UPDATE_CTRL2);
  bus.data(QUALITY_ACTIVATION);
  bus.cmd(CMD_MASTER_ACTIVATION);
  bus.waitBusy("sticky_quality_gray_fast");
  markExternalQualityComplete(display);
  qualityReferenceScreenOn = false;
}

void StickyDisplay::prepareQualityGrayscale() {
  finish();
  // This is EInkDisplay::prepareQualityGrayscale(): power down the preceding
  // BW waveform with RED bypass before writing the two grayscale planes. The
  // FreeInk SSD1677 driver does not implement this hook for Sticky.
  if (!qualityReferenceScreenOn) return;
  freeink::EpdBus& bus = display._bus;
  bus.cmd(CMD_DISPLAY_UPDATE_CTRL1);
  bus.data(CTRL1_BYPASS_RED);
  bus.cmd(CMD_DISPLAY_UPDATE_CTRL2);
  bus.data(QUALITY_PREPARE_POWERDOWN);
  bus.cmd(CMD_MASTER_ACTIVATION);
  bus.waitBusy("sticky_quality_prepare_powerdown");
  qualityReferenceScreenOn = false;
}

uint16_t StickyDisplay::getDisplayWidth() const { return display.getDisplayWidth(); }

uint16_t StickyDisplay::getDisplayHeight() const { return display.getDisplayHeight(); }

uint16_t StickyDisplay::getDisplayWidthBytes() const { return display.getDisplayWidthBytes(); }

uint32_t StickyDisplay::getBufferSize() const { return display.getBufferSize(); }
