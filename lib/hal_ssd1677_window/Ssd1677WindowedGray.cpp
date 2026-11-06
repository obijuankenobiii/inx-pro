#define private public
#include <EInkDisplay.h>
#undef private

#include "Ssd1677WindowedGray.h"

#include <BoardConfig.h>
#include <vector>

#if __has_include(<esp_rom_sys.h>)
#include <esp_rom_sys.h>
#define SSD1677_PROJECT_WINDOW_LOG(...) esp_rom_printf(__VA_ARGS__)
#else
#define SSD1677_PROJECT_WINDOW_LOG(...) Serial.printf(__VA_ARGS__)
#endif

#include "../../freeink-sdk/libs/display/FreeInkDisplay/src/lut/Ssd1677Luts.h"

namespace {
constexpr uint8_t kWriteLut = 0x32;
constexpr uint8_t kGateVoltage = 0x03;
constexpr uint8_t kSourceVoltage = 0x04;
constexpr uint8_t kWriteVcom = 0x2C;
constexpr uint8_t kBorderWaveform = 0x3C;
constexpr uint8_t kRamXRange = 0x44;
constexpr uint8_t kRamYRange = 0x45;
constexpr uint8_t kRamXCounter = 0x4E;
constexpr uint8_t kRamYCounter = 0x4F;
constexpr uint8_t kWriteBw = 0x24;
constexpr uint8_t kWriteRed = 0x26;
constexpr uint8_t kUpdateControl1 = 0x21;
constexpr uint8_t kUpdateControl2 = 0x22;
constexpr uint8_t kMasterActivation = 0x20;

const char* boardName() {
  return BoardConfig::ACTIVE.board == BoardConfig::Board::Sticky ? "sticky" : "murphy_m4";
}
}  // namespace

using EInkDisplay = freeink::FreeInkDisplay;

Ssd1677WindowedGray::Ssd1677WindowedGray(EInkDisplay& display, const uint16_t railSettleMs)
    : _display(display), _railSettleMs(railSettleMs) {}

bool Ssd1677WindowedGray::supports() const {
  return BoardConfig::ACTIVE.board == BoardConfig::Board::Sticky ||
         BoardConfig::ACTIVE.board == BoardConfig::Board::MurphyM4;
}

void Ssd1677WindowedGray::prepare(const uint16_t x, const uint16_t y, const uint16_t w, const uint16_t h) {
  _armed = supports() && w != 0 && h != 0 && x + w <= _display.getDisplayWidth() &&
           y + h <= _display.getDisplayHeight() && (x & 7u) == 0 && (w & 7u) == 0;
  _baseline = nullptr;
  if (!_armed) {
    SSD1677_PROJECT_WINDOW_LOG("[SSD1677][PROJECT-GRAY-WINDOW] reject board=%s x=%u y=%u w=%u h=%u\n", boardName(),
                              x, y, w, h);
    return;
  }

  _display.syncPendingAsync();
  _windowX = x;
  _windowY = y;
  _windowW = w;
  _windowH = h;
#ifdef EINK_DISPLAY_SINGLE_BUFFER_MODE
  _baseline = _display.frameBuffer;
#else
  _baseline = _display.frameBufferActive != nullptr ? _display.frameBufferActive : _display.frameBuffer;
#endif
  SSD1677_PROJECT_WINDOW_LOG("[SSD1677][PROJECT-GRAY-WINDOW] arm board=%s x=%u y=%u w=%u h=%u baseline=%s\n", boardName(),
                            x, y, w, h, _baseline != nullptr ? "yes" : "no");
}

void Ssd1677WindowedGray::setRamArea(const uint16_t x, const uint16_t yIn, const uint16_t w, const uint16_t h) {
  const bool mirrorX = BoardConfig::ACTIVE.orientation.mirrorX;
  const uint16_t panelHeight = _display.getDisplayHeight();
  const uint16_t xLo = x;
  const uint16_t xHi = static_cast<uint16_t>(x + w - 1);
  const uint16_t xStart = mirrorX ? xHi : xLo;
  const uint16_t xEnd = mirrorX ? xLo : xHi;
  const uint8_t dataEntry = mirrorX ? 0x00 : 0x01;
  const uint16_t y = static_cast<uint16_t>(panelHeight - yIn - h);

  _display._bus.cmd(0x11);
  _display._bus.data(dataEntry);
  _display._bus.cmd(kRamXRange);
  _display._bus.data(static_cast<uint8_t>(xStart));
  _display._bus.data(static_cast<uint8_t>(xStart >> 8));
  _display._bus.data(static_cast<uint8_t>(xEnd));
  _display._bus.data(static_cast<uint8_t>(xEnd >> 8));
  _display._bus.cmd(kRamYRange);
  _display._bus.data(static_cast<uint8_t>(y + h - 1));
  _display._bus.data(static_cast<uint8_t>((y + h - 1) >> 8));
  _display._bus.data(static_cast<uint8_t>(y));
  _display._bus.data(static_cast<uint8_t>(y >> 8));
  _display._bus.cmd(kRamXCounter);
  _display._bus.data(static_cast<uint8_t>(xStart));
  _display._bus.data(static_cast<uint8_t>(xStart >> 8));
  _display._bus.cmd(kRamYCounter);
  _display._bus.data(static_cast<uint8_t>(y + h - 1));
  _display._bus.data(static_cast<uint8_t>((y + h - 1) >> 8));
}

void Ssd1677WindowedGray::setUpdateControl1(const uint8_t first, const uint8_t second) {
  // SSD1677 command 0x21 is a two-byte register. Sending only the first byte
  // leaves the next command byte in the register and turns the activation into
  // a full/invalid update on some panel batches (notably Murphy and Sticky).
  _display._bus.cmd(kUpdateControl1);
  _display._bus.data(first);
  _display._bus.data(second);
}

void Ssd1677WindowedGray::writeRam(const uint8_t command, const uint8_t* data, const uint32_t size) {
  _display._bus.cmd(command);
  _display._bus.data(data, static_cast<uint16_t>(size));
}

void Ssd1677WindowedGray::writePlaneWindow(const uint8_t command, const uint8_t* plane) {
  // Match the working X4Pro order: stage the complete grayscale plane first,
  // then constrain only the following waveform with the requested window.
  // SSD1677 quality waveforms can consume RAM outside the write rectangle;
  // sending only window bytes leaves the rest stale and produces a black panel.
  setRamArea(0, 0, _display.getDisplayWidth(), _display.getDisplayHeight());
  writeRam(command, plane, _display.getBufferSize());
  SSD1677_PROJECT_WINDOW_LOG(
      "[SSD1677][PROJECT-GRAY-WINDOW] plane cmd=0x%02X full-stage=%ux%u bytes=%lu update-window=%u,%u %ux%u\n",
      command, _display.getDisplayWidth(), _display.getDisplayHeight(),
      static_cast<unsigned long>(_display.getBufferSize()), _windowX, _windowY, _windowW, _windowH);
}

void Ssd1677WindowedGray::copyLsb(const uint8_t* plane) {
  if (!_armed || plane == nullptr) {
    _display.copyGrayscaleLsbBuffers(plane);
    return;
  }
  _display.syncPendingAsync();
  writePlaneWindow(kWriteBw, plane);
}

void Ssd1677WindowedGray::copyMsb(const uint8_t* plane) {
  if (!_armed || plane == nullptr) {
    _display.copyGrayscaleMsbBuffers(plane);
    return;
  }
  _display.syncPendingAsync();
  writePlaneWindow(kWriteRed, plane);
}

void Ssd1677WindowedGray::loadLut(const unsigned char* lut) {
  const unsigned char* selected = lut != nullptr ? lut : freeink::lut_factory_quality;
  _display._bus.cmd(kWriteLut);
  for (uint16_t i = 0; i < 105; ++i) _display._bus.data(pgm_read_byte(&selected[i]));
  _display._bus.cmd(kGateVoltage);
  _display._bus.data(pgm_read_byte(&selected[105]));
  _display._bus.cmd(kSourceVoltage);
  _display._bus.data(pgm_read_byte(&selected[106]));
  _display._bus.data(pgm_read_byte(&selected[107]));
  _display._bus.data(pgm_read_byte(&selected[108]));
  _display._bus.cmd(kWriteVcom);
  _display._bus.data(pgm_read_byte(&selected[109]));
  _display._bus.cmd(kBorderWaveform);
  _display._bus.data(BoardConfig::ACTIVE.board == BoardConfig::Board::Sticky ? 0x80 : 0xC0);
}

bool Ssd1677WindowedGray::displayWindow(const uint16_t x, const uint16_t y, const uint16_t w, const uint16_t h,
                                        const unsigned char* lut) {
  if (!_armed) {
    SSD1677_PROJECT_WINDOW_LOG("[SSD1677][PROJECT-GRAY-WINDOW] reject activate: not armed x=%u y=%u w=%u h=%u\n", x,
                              y, w, h);
    return false;
  }
  if (x != _windowX || y != _windowY || w != _windowW || h != _windowH) {
    SSD1677_PROJECT_WINDOW_LOG(
        "[SSD1677][PROJECT-GRAY-WINDOW] reject activate: mismatch requested=%u,%u %ux%u armed=%u,%u %ux%u\n", x,
        y, w, h, _windowX, _windowY, _windowW, _windowH);
    return false;
  }

  loadLut(lut);
  // C7 is the SSD1677 full-refresh sequence; it ignores the requested
  // window for the waveform and was the reason the whole panel flashed.
  // Power the rails separately, then run the externally loaded LUT with the
  // custom fast/window sequence 0x0C.
  _display._bus.cmd(kUpdateControl2);
  _display._bus.data(0xC0);  // CLOCK_ON | ANALOG_ON, no display start
  _display._bus.cmd(kMasterActivation);
  _display._bus.waitBusy("project_gray_power_on");

  // Re-assert the window immediately before the only display-start command.
  setRamArea(_windowX, _windowY, _windowW, _windowH);
  setUpdateControl1(0x00, 0x00);
  if (_railSettleMs != 0) delay(_railSettleMs);
  const uint8_t ctrl2 = 0x0C;  // MODE_SELECT | DISPLAY_START, external LUT
  _display._bus.cmd(kUpdateControl2);
  _display._bus.data(ctrl2);
  _display._bus.cmd(kMasterActivation);
  _display._bus.waitBusy("project_gray_window");
  syncBaseline();
  _display._shadowValid = false;
  _display._redRamSynced = true;
  _display.setCustomLUT(false);
  _armed = false;
  _baseline = nullptr;
  SSD1677_PROJECT_WINDOW_LOG(
      "[SSD1677][PROJECT-GRAY-WINDOW] activate x=%u y=%u w=%u h=%u ctrl1=[00 00] power=0xC0 waveform=0x%02X lut=external\n",
      x, y, w, h, ctrl2);
  return true;
}

void Ssd1677WindowedGray::syncBaseline() {
  if (_baseline == nullptr) return;
  setRamArea(0, 0, _display.getDisplayWidth(), _display.getDisplayHeight());
  writeRam(kWriteBw, _baseline, _display.getBufferSize());
  writeRam(kWriteRed, _baseline, _display.getBufferSize());
  SSD1677_PROJECT_WINDOW_LOG("[SSD1677][PROJECT-GRAY-WINDOW] post-sync full baseline bytes=%lu no-refresh\n",
                            static_cast<unsigned long>(_display.getBufferSize()));
}
