#pragma once

#include <EInkDisplay.h>

class StickyDisplay {
 public:
  using RefreshMode = EInkDisplay::RefreshMode;

  static constexpr uint16_t DISPLAY_WIDTH = EInkDisplay::DISPLAY_WIDTH;
  static constexpr uint16_t DISPLAY_HEIGHT = EInkDisplay::DISPLAY_HEIGHT;
  static constexpr uint16_t DISPLAY_WIDTH_BYTES = EInkDisplay::DISPLAY_WIDTH_BYTES;
  static constexpr uint32_t BUFFER_SIZE = EInkDisplay::BUFFER_SIZE;

  StickyDisplay(int8_t sclk, int8_t mosi, int8_t cs, int8_t dc, int8_t rst, int8_t busy);

  void begin();
  void requestResync();
  void clearScreen(uint8_t color) const;
  void drawImage(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h, bool fromProgmem) const;

  void displayBuffer(RefreshMode mode);
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
  void prepareQualityGrayscale();

  uint16_t getDisplayWidth() const;
  uint16_t getDisplayHeight() const;
  uint16_t getDisplayWidthBytes() const;
  uint32_t getBufferSize() const;

 private:
  void finish();

  EInkDisplay display;
  bool qualityReferenceScreenOn = false;
};
