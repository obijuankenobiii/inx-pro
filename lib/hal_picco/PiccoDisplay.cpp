#include "PiccoDisplay.h"

#include <BoardConfig.h>

namespace {
EInkDisplay::RefreshMode convertRefreshMode(const uint8_t mode) {
  switch (mode) {
    case 0: return EInkDisplay::FULL_REFRESH;
    case 1: return EInkDisplay::HALF_REFRESH;
    default: return EInkDisplay::FAST_REFRESH;
  }
}

// Picco's Arduino/IDF combination cannot safely install the SDK's refresh
// BUSY ISR from the IPC task: the first SSD2677 refresh overflows ipc0's stack
// while registering the GPIO interrupt. Keep Picco on the bounded polling path
// instead. This is local to the Picco HAL and does not alter other devices.
bool piccoBusyWaitSlice(const int8_t, const uint8_t) {
  delay(1);
  return true;
}
}

PiccoDisplay::PiccoDisplay()
    : display(BoardConfig::ACTIVE.display.sclk, BoardConfig::ACTIVE.display.mosi, BoardConfig::ACTIVE.display.cs,
              BoardConfig::ACTIVE.display.dc, BoardConfig::ACTIVE.display.rst, BoardConfig::ACTIVE.display.busy),
      windowedGray(display) {}

void PiccoDisplay::begin() {
  // FreeInkDisplay selects the Picco panel and pins from BoardConfig::ACTIVE.
  // Do not call SPI.begin() here: the SDK owns the panel bus and Picco's SD is
  // native 4-bit SDMMC, not a shared SPI MISO line.
  display.setBusyWaitSliceHook(piccoBusyWaitSlice);
  display.begin();
  // A freshly flashed Picco can inherit an unknown controller RAM state from
  // the previous image. Invalidate the driver's grayscale/baseline state before
  // BootActivity paints its first frame so the first refresh is self-contained.
  display.requestResync();
  INX_SERIAL.printf("[PICCO][DISPLAY] controller=%s variant=%u\n", display.getControllerName() ? display.getControllerName() : "UNKNOWN",
                    BoardConfig::ACTIVE.displayControllerVariant);
}

void PiccoDisplay::clearScreen(const uint8_t color) const {
  display.clearScreen(color);
  writeBufferDirty_ = true;
}

void PiccoDisplay::drawImage(const uint8_t* imageData, const uint16_t x, const uint16_t y, const uint16_t w,
                             const uint16_t h, const bool fromProgmem) const {
  display.drawImage(imageData, x, y, w, h, fromProgmem);
  writeBufferDirty_ = true;
}

void PiccoDisplay::displayBuffer(const RefreshMode mode) {
  display.displayBuffer(convertRefreshMode(mode));
  // The full-frame handoff has consumed the writable page. A future sync is
  // allowed to seed it from the now-active page again.
  writeBufferDirty_ = false;
}

void PiccoDisplay::manualRefresh() {
  // A manual page refresh is explicitly a refresh of the frame already on the
  // panel. It must override a pre-rendered writable page, unlike the normal
  // grayscale handoff where that prepared page must be preserved.
  display.syncWriteBufferFromActive();
  writeBufferDirty_ = false;
  display.displayBuffer(EInkDisplay::HALF_REFRESH);
  writeBufferDirty_ = false;
}

void PiccoDisplay::displayWindow(const uint16_t x, const uint16_t y, const uint16_t w, const uint16_t h) {
  display.displayWindow(x, y, w, h);
}

void PiccoDisplay::displayBufferAsync(const RefreshMode mode) {
  display.displayBufferAsync(convertRefreshMode(mode));
  writeBufferDirty_ = false;
}

bool PiccoDisplay::refreshBusy() { return display.refreshBusy(); }

void PiccoDisplay::refreshDisplay(const RefreshMode mode, const bool turnOffScreen) {
  display.refreshDisplay(convertRefreshMode(mode), turnOffScreen);
}

void PiccoDisplay::syncWriteBufferFromActive() const {
  if (writeBufferDirty_) {
    INX_SERIAL.printf("[PICCO][BUFFER] preserve prepared write buffer; skip active sync\n");
    return;
  }
  display.syncWriteBufferFromActive();
}

void PiccoDisplay::deepSleep() { display.deepSleep(); }

uint8_t* PiccoDisplay::getFrameBuffer() const { return display.getFrameBuffer(); }

void PiccoDisplay::copyGrayscaleBuffers(const uint8_t* lsbBuffer, const uint8_t* msbBuffer) {
  display.copyGrayscaleBuffers(lsbBuffer, msbBuffer);
}

void PiccoDisplay::copyGrayscaleLsbBuffers(const uint8_t* lsbBuffer) {
  if (windowedGray.isArmed()) windowedGray.copyLsb(lsbBuffer);
  else display.copyGrayscaleLsbBuffers(lsbBuffer);
}

void PiccoDisplay::copyGrayscaleMsbBuffers(const uint8_t* msbBuffer) {
  if (windowedGray.isArmed()) windowedGray.copyMsb(msbBuffer);
  else display.copyGrayscaleMsbBuffers(msbBuffer);
}

void PiccoDisplay::cleanupGrayscaleBuffers(const uint8_t* bwBuffer) {
  display.cleanupGrayscaleBuffers(bwBuffer);
  if (windowedGray.takeCompletedWindow()) {
    windowedGray.syncDualBufferBaseline();
    writeBufferDirty_ = false;
  }
}

void PiccoDisplay::displayGrayBuffer(const bool quality) {
  // Picco's panel-specific gray waveform is selected by the FreeInk driver.
  // `factoryMode` requests its high-quality absolute waveform; no Sticky LUT
  // or Sticky waveform sequence is used here.
  display.displayGrayBuffer(false, nullptr, quality);
}

void PiccoDisplay::displayGrayBufferFastQuality() { display.displayGrayBuffer(false, nullptr, true); }

void PiccoDisplay::displayGrayBufferWindow(const uint16_t x, const uint16_t y, const uint16_t w, const uint16_t h,
                                           const bool quality, const bool fastQuality) {
  if (!quality || !windowedGray.displayWindow(x, y, w, h, fastQuality)) display.displayGrayBuffer(false, nullptr, quality);
}

bool PiccoDisplay::supportsWindowedGray() const { return windowedGray.supports(); }

void PiccoDisplay::displayGrayscaleBase(const RefreshMode mode, const bool turnOffScreen) {
  display.displayGrayscaleBase(convertRefreshMode(mode), turnOffScreen);
}

void PiccoDisplay::prepareQualityGrayscale() {
  // The prepared-reader path may intentionally skip its preceding B/W
  // refresh. The writable buffer then contains the new page while the panel
  // still contains the previous one. Commit that complete B/W page before
  // starting an HQ pass; otherwise a windowed waveform changes only the image
  // rectangle and leaves the old page's text outside the window.
  if (writeBufferDirty_) {
    INX_SERIAL.printf("[PICCO][BUFFER] commit prepared B/W page before HQ\n");
    display.displayBuffer(EInkDisplay::FAST_REFRESH);
    writeBufferDirty_ = false;
  }

  // Picco HQ images are complete four-level frames. Enter the SDK driver's
  // absolute mode before the renderer writes the LSB/MSB planes; the overlay
  // mode is reserved for medium image rendering and text anti-aliasing.
  display.displayGrayscaleBase(freeink::GrayscaleMode::Absolute, EInkDisplay::HALF_REFRESH, false);
}

void PiccoDisplay::prepareGrayscaleWindow(const uint16_t x, const uint16_t y, const uint16_t w, const uint16_t h) {
  prepareQualityGrayscale();
  windowedGray.prepare(x, y, w, h);
}

uint16_t PiccoDisplay::getDisplayWidth() const { return display.getDisplayWidth(); }
uint16_t PiccoDisplay::getDisplayHeight() const { return display.getDisplayHeight(); }
uint16_t PiccoDisplay::getDisplayWidthBytes() const { return display.getDisplayWidthBytes(); }
uint32_t PiccoDisplay::getBufferSize() const { return display.getBufferSize(); }
