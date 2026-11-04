#pragma once

#include <Arduino.h>

#include "PiccoDisplay.h"

/** Application display interface for the BOOX/Onyx Picco. */
class HalDisplay {
 public:
  HalDisplay() = default;
  ~HalDisplay() = default;

  enum RefreshMode { FULL_REFRESH, HALF_REFRESH, FAST_REFRESH, STRONG_FAST_REFRESH, MANUAL_REFRESH };

  void begin();
  static constexpr uint16_t DISPLAY_WIDTH = 800;
  static constexpr uint16_t DISPLAY_HEIGHT = 480;
  static constexpr uint16_t DISPLAY_WIDTH_BYTES = DISPLAY_WIDTH / 8;
  static constexpr uint32_t BUFFER_SIZE = DISPLAY_WIDTH_BYTES * DISPLAY_HEIGHT;

  void clearScreen(uint8_t color = 0xFF) const;
  void drawImage(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                 bool fromProgmem = false) const;
  void displayBuffer(RefreshMode mode = FAST_REFRESH);
  void displayWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h, RefreshMode mode = FAST_REFRESH);
  void displayBufferAsync(RefreshMode mode = FAST_REFRESH);
  bool refreshBusy();
  void refreshDisplay(RefreshMode mode = FAST_REFRESH, bool turnOffScreen = false);
  void syncWriteBufferFromActive() const;
  void deepSleep();
  uint8_t* getFrameBuffer() const;
  void copyGrayscaleBuffers(const uint8_t* lsbBuffer, const uint8_t* msbBuffer);
  void copyGrayscaleLsbBuffers(const uint8_t* lsbBuffer);
  void copyGrayscaleMsbBuffers(const uint8_t* msbBuffer);
  void cleanupGrayscaleBuffers(const uint8_t* bwBuffer);
  void displayGrayBuffer(bool quality = false, bool trackForRevert = true);
  void displayGrayBufferWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h, bool quality = false,
                               bool fastQuality = false);
  bool supportsWindowedQuality() const;
  void displayGrayBufferFastQuality();
  void displayGrayscaleBase(RefreshMode fallback = HALF_REFRESH, bool turnOffScreen = false);
  void prepareQualityGrayscale();
  void prepareQualityGrayscaleWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
  uint16_t getDisplayWidth() const;
  uint16_t getDisplayHeight() const;
  uint16_t getDisplayWidthBytes() const;
  uint32_t getBufferSize() const;

 private:
  PiccoDisplay display;
};
