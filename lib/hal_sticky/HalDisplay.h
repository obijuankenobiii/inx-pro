#pragma once

/**
 * @file HalDisplay.h
 * @brief Sticky variant of HalDisplay. Same public interface as lib/hal/HalDisplay.h
 * so src/ compiles unchanged; the implementation delegates to freeink-sdk's
 * FreeInkDisplay (aliased to EInkDisplay), whose Sticky BoardProfile carries the
 * verified pin/panel config — see freeink-sdk/libs/hardware/BoardConfig.
 */

#include <Arduino.h>
#include <StickyDisplay.h>

class HalDisplay {
 public:
  HalDisplay();

  ~HalDisplay();

  enum RefreshMode { FULL_REFRESH, HALF_REFRESH, FAST_REFRESH, STRONG_FAST_REFRESH, MANUAL_REFRESH };

  void begin();

  static constexpr uint16_t DISPLAY_WIDTH = StickyDisplay::DISPLAY_WIDTH;
  static constexpr uint16_t DISPLAY_HEIGHT = StickyDisplay::DISPLAY_HEIGHT;
  static constexpr uint16_t DISPLAY_WIDTH_BYTES = DISPLAY_WIDTH / 8;
  static constexpr uint32_t BUFFER_SIZE = DISPLAY_WIDTH_BYTES * DISPLAY_HEIGHT;

  void clearScreen(uint8_t color = 0xFF) const;
  void drawImage(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                 bool fromProgmem = false) const;

  void displayBuffer(RefreshMode mode = RefreshMode::FAST_REFRESH);
  /** Starts a panel refresh and returns while the waveform is running. */
  void displayBufferAsync(RefreshMode mode = RefreshMode::FAST_REFRESH);
  void refreshDisplay(RefreshMode mode = RefreshMode::FAST_REFRESH, bool turnOffScreen = false);
  /** Copies the displayed dual-buffer frame into the writable framebuffer before partial redraws. */
  void syncWriteBufferFromActive() const;

  void deepSleep();

  uint8_t* getFrameBuffer() const;

  void copyGrayscaleBuffers(const uint8_t* lsbBuffer, const uint8_t* msbBuffer);
  void copyGrayscaleLsbBuffers(const uint8_t* lsbBuffer);
  void copyGrayscaleMsbBuffers(const uint8_t* msbBuffer);
  void cleanupGrayscaleBuffers(const uint8_t* bwBuffer);

  void displayGrayBuffer(bool quality = false, bool trackForRevert = true);
  void displayGrayBufferFastQuality();
  void prepareQualityGrayscale();

  uint16_t getDisplayWidth() const;
  uint16_t getDisplayHeight() const;
  uint16_t getDisplayWidthBytes() const;
  uint32_t getBufferSize() const;
 private:
  StickyDisplay display;
};
