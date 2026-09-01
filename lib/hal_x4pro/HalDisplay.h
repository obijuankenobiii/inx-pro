#pragma once

/**
 * @file HalDisplay.h
 * @brief Xteink X4 Pro variant of HalDisplay.
 *
 * Same public interface as lib/hal_sticky/HalDisplay.h so src/ compiles unchanged,
 * but this one talks to freeink-sdk's FreeInkDisplay (aliased to EInkDisplay)
 * DIRECTLY - there is no StickyDisplay layer in between, and deliberately so:
 *
 *  - No custom waveform LUTs. StickyDisplay preloads Sticky's hand-corrected X4
 *    quality/fast LUTs on every grayscale pass. The X4 Pro develops the image on
 *    the panel's own OTP waveform - "no custom LUT/voltages/PMIC needed", see
 *    freeink-sdk/docs/xteink-x4pro-support.md#display--ssd1677-800480.
 *  - No `#define private public`. StickyDisplay reaches into EInkDisplay's private
 *    shadow flags to undo its externally-applied LUT. Nothing here applies one.
 *  - No async+poll BUSY dance. StickyDisplay::finish() exists to paper over a
 *    Sticky-specific race where the BUSY completion edge can land before FreeInk
 *    arms its ISR waiter; it then polls BUSY in an unbounded loop. The X4 Pro has
 *    no such race, so refreshes use the SDK's plain synchronous displayBuffer().
 *  - No shared-SPI fixup. Sticky's HalDisplay::begin() re-runs
 *    SPI.begin(sclk, BoardConfig::ACTIVE.sd.miso, mosi, -1) because its panel and
 *    SD card share one SPI bus. On the X4 Pro sd.miso is GPIO40, which is also the
 *    live SDMMC DAT0 line - doing that binds the mounted card's data pin into the
 *    SPI peripheral and every subsequent SD write fails with 0x107 (ESP_ERR_TIMEOUT).
 *    The X4 Pro shares nothing: the panel is SPI on 12/11, the card is SDMMC on
 *    41/42/40, and the SDK's SSD1677 driver reports spiMiso() == -1, so EpdBus
 *    brings the bus up correctly on its own.
 *
 * Pin/panel config comes from BoardConfig::XTEINK_X4_PRO.
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
  void clearBothBuffers(uint8_t color = 0xFF) const;
  void finishPendingRefresh() const;
  void drawImage(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                 bool fromProgmem = false) const;

  void displayBuffer(RefreshMode mode = RefreshMode::FAST_REFRESH);
  /** Starts a panel refresh and returns while the waveform is running. */
  void displayBufferAsync(RefreshMode mode = RefreshMode::FAST_REFRESH);
  /** True while an async panel refresh is still running. */
  bool refreshBusy();
  void refreshDisplay(RefreshMode mode = RefreshMode::FAST_REFRESH, bool turnOffScreen = false);
  /** Copies the displayed dual-buffer frame into the writable framebuffer before partial
   *  redraws. The X4 Pro builds WITHOUT EINK_DISPLAY_SINGLE_BUFFER_MODE, so this really
   *  does memcpy - call it before drawing a partial update, never after. */
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

  /** Force which OEM AA grayscale table the UltraChip drivers use (0x02 or 0x68).
   *  No effect on SSD1677 units. Both values are OEM-validated waveforms. */
  void forceGrayWaveformVariant(uint8_t variant);

  uint16_t getDisplayWidth() const;
  uint16_t getDisplayHeight() const;
  uint16_t getDisplayWidthBytes() const;
  uint32_t getBufferSize() const;

 private:
  EInkDisplay display;
};

