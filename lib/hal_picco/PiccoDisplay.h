#pragma once

#include <Arduino.h>
#include <EInkDisplay.h>

#include "PiccoSsd2677WindowedGray.h"

/**
 * Picco display adapter.
 *
 * This deliberately talks to FreeInkDisplay directly. The Picco has its own
 * SSD1677/SSD2677 panel selection and waveform path in the FreeInk SDK; it
 * must not inherit Sticky's custom LUT wrapper or Sticky's refresh policy.
 */
class PiccoDisplay final {
 public:
  using RefreshMode = EInkDisplay::RefreshMode;

  PiccoDisplay();

  void begin();
  void clearScreen(uint8_t color) const;
  void drawImage(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                 bool fromProgmem) const;
  void displayBuffer(RefreshMode mode);
  void manualRefresh();
  void displayWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
  void displayBufferAsync(RefreshMode mode);
  bool refreshBusy();
  void refreshDisplay(RefreshMode mode, bool turnOffScreen);
  void syncWriteBufferFromActive() const;
  void deepSleep();
  uint8_t* getFrameBuffer() const;
  void copyGrayscaleBuffers(const uint8_t* lsbBuffer, const uint8_t* msbBuffer);
  void copyGrayscaleLsbBuffers(const uint8_t* lsbBuffer);
  void copyGrayscaleMsbBuffers(const uint8_t* msbBuffer);
  void cleanupGrayscaleBuffers(const uint8_t* bwBuffer);
  void displayGrayBuffer(bool quality);
  void displayGrayBufferFastQuality();
  void displayGrayBufferWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h, bool quality, bool fastQuality);
  bool supportsWindowedGray() const;
  void displayGrayscaleBase(RefreshMode mode, bool turnOffScreen);
  void prepareQualityGrayscale();
  void prepareGrayscaleWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
  uint16_t getDisplayWidth() const;
  uint16_t getDisplayHeight() const;
  uint16_t getDisplayWidthBytes() const;
  uint32_t getBufferSize() const;

 private:
  EInkDisplay display;
  PiccoSsd2677WindowedGray windowedGray;
  // A prepared page is rendered directly into Picco's writable dual buffer
  // while the previous page remains active. Do not replace that page with the
  // active buffer when a later grayscale pass asks for a baseline sync.
  mutable bool writeBufferDirty_ = false;
};
