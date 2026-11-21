#include "PiccoDisplay.h"

#include <BoardConfig.h>
#include <HalGPIO.h>

extern HalGPIO gpio;

namespace {
EInkDisplay::RefreshMode convertRefreshMode(const uint8_t mode) {
  switch (mode) {
    case 0: return EInkDisplay::FULL_REFRESH;
    case 1: return EInkDisplay::HALF_REFRESH;
    default: return EInkDisplay::FAST_REFRESH;
  }
}





bool piccoBusyWaitSlice(const int8_t, const uint8_t) {
  gpio.updateDuringDisplay();
  delay(1);
  return true;
}
}

PiccoDisplay::PiccoDisplay()
    : display(BoardConfig::ACTIVE.display.sclk, BoardConfig::ACTIVE.display.mosi, BoardConfig::ACTIVE.display.cs,
              BoardConfig::ACTIVE.display.dc, BoardConfig::ACTIVE.display.rst, BoardConfig::ACTIVE.display.busy),
      windowedGray(display) {}

void PiccoDisplay::begin() {



  display.setBusyWaitSliceHook(piccoBusyWaitSlice);
  display.begin();



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


  writeBufferDirty_ = false;
}

void PiccoDisplay::manualRefresh() {



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
  const_cast<EInkDisplay&>(display).waitRefreshComplete();
  display.syncWriteBufferFromActive();
  writeBufferDirty_ = false;
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
  const unsigned long startedAt = millis();
  unsigned long bwCommitMs = 0;





  if (writeBufferDirty_) {
    INX_SERIAL.printf("[PICCO][BUFFER] commit prepared B/W page before HQ\n");
    display.displayBuffer(EInkDisplay::FAST_REFRESH);
    writeBufferDirty_ = false;
    bwCommitMs = millis() - startedAt;
  }






  syncWriteBufferFromActive();
  writeBufferDirty_ = false;




  display.displayGrayscaleBase(freeink::GrayscaleMode::Absolute, EInkDisplay::HALF_REFRESH, false);
  const unsigned long grayscaleBaseMs = millis() - startedAt - bwCommitMs;
  INX_SERIAL.printf("[PICCO][HQ-TIMING] prepare bw_commit=%lu ms grayscale_base=%lu ms total=%lu ms\n", bwCommitMs,
                    grayscaleBaseMs, millis() - startedAt);
}

void PiccoDisplay::prepareGrayscaleWindow(const uint16_t x, const uint16_t y, const uint16_t w, const uint16_t h) {
  prepareQualityGrayscale();
  windowedGray.prepare(x, y, w, h);
}

uint16_t PiccoDisplay::getDisplayWidth() const { return display.getDisplayWidth(); }
uint16_t PiccoDisplay::getDisplayHeight() const { return display.getDisplayHeight(); }
uint16_t PiccoDisplay::getDisplayWidthBytes() const { return display.getDisplayWidthBytes(); }
uint32_t PiccoDisplay::getBufferSize() const { return display.getBufferSize(); }
