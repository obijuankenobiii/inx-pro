#define private public
#include <EInkDisplay.h>
#undef private

#include "PiccoSsd2677WindowedGray.h"

#include <BoardConfig.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cstring>

#define private public
#include "../../freeink-sdk/libs/display/FreeInkDisplay/src/driver/Ssd2677Driver.h"
#undef private
#include "../../freeink-sdk/libs/display/FreeInkDisplay/src/lut/Ssd2677Luts.h"

namespace {
constexpr uint8_t kPanelWindow = 0x83;
constexpr uint8_t kData = 0x10;
constexpr uint8_t kLut = 0x20;
constexpr uint8_t kPowerOn = 0x04;
constexpr uint8_t kRefresh = 0x12;
constexpr uint16_t kGateOffset = 0x00C8;
constexpr uint16_t kLutLength = 535;

inline void packPixels(const uint8_t msb, const uint8_t lsb, uint8_t* out) {
  uint16_t packed = 0;
  for (int bit = 7; bit >= 0; --bit) {
    packed = static_cast<uint16_t>((packed << 2) | (((msb >> bit) & 1u) << 1) | ((lsb >> bit) & 1u));
  }
  out[0] = static_cast<uint8_t>(packed >> 8);
  out[1] = static_cast<uint8_t>(packed);
}
}  // namespace

PiccoSsd2677WindowedGray::PiccoSsd2677WindowedGray(freeink::FreeInkDisplay& display) : _display(display) {}

PiccoSsd2677WindowedGray::~PiccoSsd2677WindowedGray() {
  if (_lsb) heap_caps_free(_lsb);
  if (_msb) heap_caps_free(_msb);
}

bool PiccoSsd2677WindowedGray::supports() const {
  return BoardConfig::ACTIVE.board == BoardConfig::Board::Picco && _display.getControllerName() != nullptr &&
         std::strcmp(_display.getControllerName(), "SSD2677") == 0;
}

bool PiccoSsd2677WindowedGray::ensureBuffers(const size_t bytes) {
  if (_lsb && _msb && _bufferBytes >= bytes) return true;
  if (_lsb) {
    heap_caps_free(_lsb);
    _lsb = nullptr;
  }
  if (_msb) {
    heap_caps_free(_msb);
    _msb = nullptr;
  }
  _bufferBytes = 0;
  if (!_lsb) _lsb = static_cast<uint8_t*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!_msb) _msb = static_cast<uint8_t*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (_lsb && _msb) {
    _bufferBytes = bytes;
    return true;
  }
  if (_lsb) {
    heap_caps_free(_lsb);
    _lsb = nullptr;
  }
  if (_msb) {
    heap_caps_free(_msb);
    _msb = nullptr;
  }
  return false;
}

void PiccoSsd2677WindowedGray::prepare(const uint16_t x, const uint16_t y, const uint16_t w, const uint16_t h) {
  _armed = false;
  _completedWindow = false;
  if (!supports() || !w || !h || x >= _display.getDisplayWidth() || y >= _display.getDisplayHeight()) return;

  const uint16_t endX = static_cast<uint16_t>(std::min<uint32_t>(_display.getDisplayWidth(),
                                                                  static_cast<uint32_t>(x) + w));
  const uint16_t endY = static_cast<uint16_t>(std::min<uint32_t>(_display.getDisplayHeight(),
                                                                  static_cast<uint32_t>(y) + h));
  const uint16_t alignedX = static_cast<uint16_t>(x & ~7u);
  const uint16_t alignedEndX = static_cast<uint16_t>((static_cast<uint32_t>(endX) + 7u) & ~7u);
  if (alignedEndX > _display.getDisplayWidth() || endY <= y) return;

  _windowX = alignedX;
  _windowY = y;
  _windowW = static_cast<uint16_t>(alignedEndX - alignedX);
  _windowH = static_cast<uint16_t>(endY - y);
  _rowBytes = static_cast<uint16_t>(_windowW / 8);
  // PTLW limits the panel scan/refresh area. It does not move the DTM RAM
  // write origin: command 0x10 still consumes a complete 800x480 frame.
  // Stage both full planes so the controller RAM remains correctly addressed,
  // while DRF still refreshes only the requested window.
  if (!_rowBytes || !_windowH || !ensureBuffers(_display.getBufferSize())) return;

  _display.syncPendingAsync();
  _armed = true;
  Serial.printf("[PICCO][HQ-WINDOW] arm x=%u y=%u w=%u h=%u bytes=%u\n", _windowX, _windowY, _windowW, _windowH,
                static_cast<unsigned>(_rowBytes * _windowH));
}

void PiccoSsd2677WindowedGray::copyLsb(const uint8_t* plane) {
  if (!_armed || !plane) return;
  memcpy(_lsb, plane, _display.getBufferSize());
}

void PiccoSsd2677WindowedGray::copyMsb(const uint8_t* plane) {
  if (!_armed || !plane) return;
  memcpy(_msb, plane, _display.getBufferSize());
}

void PiccoSsd2677WindowedGray::setPartialWindow() {
  const uint16_t xEnd = static_cast<uint16_t>(_windowX + _windowW - 1);
  const uint16_t yStart = static_cast<uint16_t>(kGateOffset + _windowY);
  const uint16_t yEnd = static_cast<uint16_t>(yStart + _windowH - 1);
  const uint8_t data[9] = {
      static_cast<uint8_t>(_windowX >> 8), static_cast<uint8_t>(_windowX),
      static_cast<uint8_t>(xEnd >> 8), static_cast<uint8_t>(xEnd),
      static_cast<uint8_t>(yStart >> 8), static_cast<uint8_t>(yStart),
      static_cast<uint8_t>(yEnd >> 8), static_cast<uint8_t>(yEnd), 0x01};
  _display._bus.cmd(kPanelWindow);
  _display._bus.data(data, sizeof(data));
}

void PiccoSsd2677WindowedGray::restoreFullWindow() {
  // Restore the exact range used by the stock Picco initializer. The next
  // normal full-frame transfer must never inherit the image PTLW.
  static constexpr uint8_t fullWindow[9] = {0x00, 0x00, 0x03, 0x1F, 0x00, 0xC8, 0x02, 0xA8, 0x01};
  _display._bus.cmd(kPanelWindow);
  _display._bus.data(fullWindow, sizeof(fullWindow));
}

void PiccoSsd2677WindowedGray::writePackedWindow() {
  static uint8_t chunk[1024];
  _display._bus.cmd(kData);
  auto transaction = _display._bus.beginTxn();
  uint16_t used = 0;
  const uint16_t fullRowBytes = _display.getDisplayWidthBytes();
  const uint16_t fullHeight = _display.getDisplayHeight();
  for (uint16_t row = 0; row < fullHeight; ++row) {
    const uint8_t* msb = _msb + static_cast<size_t>(row) * fullRowBytes;
    const uint8_t* lsb = _lsb + static_cast<size_t>(row) * fullRowBytes;
    for (uint16_t column = 0; column < fullRowBytes; ++column) {
      packPixels(msb[column], lsb[column], &chunk[used]);
      used = static_cast<uint16_t>(used + 2);
      if (used == sizeof(chunk)) {
        transaction.writeBytes(chunk, used);
        used = 0;
      }
    }
  }
  if (used) transaction.writeBytes(chunk, used);
}

void PiccoSsd2677WindowedGray::uploadImageLut() {
  _display._bus.cmd(kLut);
  _display._bus.data(lut_picco_se0400_image, kLutLength);
}

void PiccoSsd2677WindowedGray::finishDriverState() {
  auto* driver = static_cast<freeink::Ssd2677Driver*>(_display._driver);
  if (driver) {
    driver->_needsPowerOn = false;
    driver->_needsInitialFull = true;
    driver->_absoluteGray = false;
  }
  _display._shadowValid = false;
  _display._redRamSynced = false;
}

bool PiccoSsd2677WindowedGray::takeCompletedWindow() {
  const bool completed = _completedWindow;
  _completedWindow = false;
  return completed;
}

void PiccoSsd2677WindowedGray::syncDualBufferBaseline() {
#ifndef EINK_DISPLAY_SINGLE_BUFFER_MODE
  // The partial waveform does not swap FreeInkDisplay's host buffers. After
  // cleanup, frameBuffer is the restored B/W page while frameBufferActive is
  // still the pre-window pointer. Keep both copies identical so the next
  // syncWriteBufferFromActive() cannot resurrect the previous page.
  if (_display.frameBuffer && _display.frameBufferActive && _display.bufferSize) {
    memcpy(_display.frameBufferActive, _display.frameBuffer, _display.bufferSize);
    Serial.printf("[PICCO][HQ-WINDOW] dual-buffer baseline synced bytes=%u\n",
                  static_cast<unsigned>(_display.bufferSize));
  }
#endif
}

bool PiccoSsd2677WindowedGray::displayWindow(const uint16_t x, const uint16_t y, const uint16_t w,
                                             const uint16_t h, const bool) {
  if (!_armed || x != _windowX || y != _windowY || w != _windowW || h != _windowH) {
    _armed = false;
    return false;
  }

  _display.syncPendingAsync();
  // DTM (0x10) must be written while PTLW still describes the full panel.
  // PTLW is opened only after both complete planes are staged, immediately
  // before the LUT/PON/DRF sequence. Opening PTLW before DTM causes the
  // controller to interpret the full stream with the partial cursor and
  // produces corrupted/repeated image data.
  restoreFullWindow();
  writePackedWindow();
  setPartialWindow();
  // Keep the proven SSD2677 order: load the waveform, power the analog
  // section, and activate refresh for the already-staged partial window.
  uploadImageLut();
  _display._bus.cmd(kPowerOn);
  _display._bus.data(0x00);
  _display._bus.waitBusy("picco_gray_window_power_on");
  _display._bus.cmd(kRefresh);
  _display._bus.data(0x00);
  _display._bus.waitRefreshComplete("picco_gray_window");
  restoreFullWindow();
  finishDriverState();
  _armed = false;
  _completedWindow = true;
  Serial.printf("[PICCO][HQ-WINDOW] refresh window x=%u y=%u w=%u h=%u full-stage=%ux%u bytes=%u\n", x, y, w, h,
                _display.getDisplayWidth(), _display.getDisplayHeight(), static_cast<unsigned>(_display.getBufferSize()));
  return true;
}
