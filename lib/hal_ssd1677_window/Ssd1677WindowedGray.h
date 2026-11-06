#pragma once

#include <Arduino.h>

namespace freeink {
class FreeInkDisplay;
}

// Project-owned SSD1677 adapter. The vendored FreeInkDisplay API intentionally
// remains unchanged; Sticky and Murphy use this class for their windowed
// quality-image path.
class Ssd1677WindowedGray {
 public:
  explicit Ssd1677WindowedGray(freeink::FreeInkDisplay& display, uint16_t railSettleMs = 0);

  bool supports() const;
  void prepare(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
  void copyLsb(const uint8_t* plane);
  void copyMsb(const uint8_t* plane);
  bool displayWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const unsigned char* lut);
  bool isArmed() const { return _armed; }

 private:
  void setRamArea(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
  void setUpdateControl1(uint8_t first, uint8_t second);
  void writeRam(uint8_t command, const uint8_t* data, uint32_t size);
  void writePlaneWindow(uint8_t command, const uint8_t* plane);
  void loadLut(const unsigned char* lut);
  void syncBaseline();

  freeink::FreeInkDisplay& _display;
  uint16_t _railSettleMs;
  uint16_t _windowX = 0;
  uint16_t _windowY = 0;
  uint16_t _windowW = 0;
  uint16_t _windowH = 0;
  const uint8_t* _baseline = nullptr;
  bool _armed = false;
};
