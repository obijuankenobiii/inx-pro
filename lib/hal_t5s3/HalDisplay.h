#pragma once

/**
 * @file HalDisplay.h
 * @brief LilyGo T5 S3 E-Paper Pro / Pro Lite variant of HalDisplay.
 *
 * Same public interface as lib/hal_sticky/HalDisplay.h so src/ compiles unchanged.
 * Like lib/hal_x4pro it talks to freeink-sdk's FreeInkDisplay (aliased to
 * EInkDisplay) directly, but the panel underneath is a different class entirely:
 *
 *  - The ED047TC1 is a raw 960x540 16-gray parallel panel with NO on-glass
 *    controller or RAM. freeink-sdk's LgfxEpdDriver expands the 1-bpp framebuffer
 *    into an 8-bit LovyanGFX canvas in PSRAM and pushes that over the S3's LCD
 *    (i80) bus. There is no SPI bus, no BUSY pin and no controller LUT to load, so
 *    none of the X4 Pro's register-level waveform work applies here.
 *  - Grayscale is an overlay: the LSB/MSB planes darken selected pixels of the
 *    canvas the last B/W push left behind (see ImageToneMap.cpp for the codes).
 *    "Quality" and "medium" differ only in which tones the renderer puts in the
 *    planes; the driver pushes both with the same waveform as the base.
 *  - Panel power (TPS65185 PMIC behind a PCA9535 expander) is sequenced by the
 *    SDK's BoardT5S3 hooks, which need HalGPIO::begin() to have started I2C first.
 *
 * Geometry comes from BoardConfig::LILYGO_T5S3 (960x540, landscape native). The
 * static DISPLAY_* constants are the SDK's legacy 800x480 values - use the runtime
 * getters for anything sized to the panel.
 */

#include <Arduino.h>
#include <EInkDisplay.h>

class HalDisplay {
 public:
  HalDisplay();
  ~HalDisplay();

  enum RefreshMode { FULL_REFRESH, HALF_REFRESH, FAST_REFRESH, STRONG_FAST_REFRESH, MANUAL_REFRESH };

  void begin();

  static constexpr uint16_t DISPLAY_WIDTH = EInkDisplay::DISPLAY_WIDTH;
  static constexpr uint16_t DISPLAY_HEIGHT = EInkDisplay::DISPLAY_HEIGHT;
  static constexpr uint16_t DISPLAY_WIDTH_BYTES = DISPLAY_WIDTH / 8;
  static constexpr uint32_t BUFFER_SIZE = DISPLAY_WIDTH_BYTES * DISPLAY_HEIGHT;

  void clearScreen(uint8_t color = 0xFF) const;
  void drawImage(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                 bool fromProgmem = false) const;

  void displayBuffer(RefreshMode mode = RefreshMode::FAST_REFRESH);
  void displayBufferAsync(RefreshMode mode = RefreshMode::FAST_REFRESH);
  bool refreshBusy();
  void refreshDisplay(RefreshMode mode = RefreshMode::FAST_REFRESH, bool turnOffScreen = false);
  void syncWriteBufferFromActive() const;

  void deepSleep();

  uint8_t* getFrameBuffer() const;

  void copyGrayscaleBuffers(const uint8_t* lsbBuffer, const uint8_t* msbBuffer);
  void copyGrayscaleLsbBuffers(const uint8_t* lsbBuffer);
  void copyGrayscaleMsbBuffers(const uint8_t* msbBuffer);
  void cleanupGrayscaleBuffers(const uint8_t* bwBuffer);

  void displayGrayBuffer(bool quality = false, bool trackForRevert = true);
  void displayGrayBufferFastQuality();
  void displayGrayscaleBase(RefreshMode fallback = HALF_REFRESH, bool turnOffScreen = false);
  void prepareQualityGrayscale();

  uint16_t getDisplayWidth() const;
  uint16_t getDisplayHeight() const;
  uint16_t getDisplayWidthBytes() const;
  uint32_t getBufferSize() const;

 private:
  EInkDisplay display;
};
