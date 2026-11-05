#pragma once

#include <Arduino.h>
#include <EInkDisplay.h>

// Project-owned Picco HQ adapter. The vendored FreeInk SSD2677 driver remains
// the source of truth for normal/full-frame updates; this class is used only
// for an explicitly armed EPUB HQ image window.
class PiccoSsd2677WindowedGray {
 public:
  explicit PiccoSsd2677WindowedGray(freeink::FreeInkDisplay& display);
  ~PiccoSsd2677WindowedGray();

  bool supports() const;
  void prepare(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
  void copyLsb(const uint8_t* plane);
  void copyMsb(const uint8_t* plane);
  bool displayWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h, bool fastQuality);
  bool takeCompletedWindow();
  void syncDualBufferBaseline();
  bool isArmed() const { return _armed; }

 private:
  bool ensureBuffers(size_t bytes);
  void setPartialWindow();
  void restoreFullWindow();
  void writePackedWindow();
  void uploadImageLut(bool fastQuality);
  void finishDriverState();

  freeink::FreeInkDisplay& _display;
  uint16_t _windowX = 0;
  uint16_t _windowY = 0;
  uint16_t _windowW = 0;
  uint16_t _windowH = 0;
  uint16_t _rowBytes = 0;
  size_t _bufferBytes = 0;
  uint8_t* _lsb = nullptr;
  uint8_t* _msb = nullptr;
  bool _armed = false;
  bool _completedWindow = false;
};
