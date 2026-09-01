/**
 * @file HalDisplay.cpp
 * @brief Xteink X4 Pro HalDisplay — thin passthrough to freeink-sdk's FreeInkDisplay.
 *
 * See HalDisplay.h for why this does NOT reuse the Sticky display wrapper.
 */

#include <BoardConfig.h>
#include <XteinkDetect.h>
#include <esp_idf_version.h>

#define private public
#include <HalDisplay.h>
#undef private
#include <HalGPIO.h>

#define private public
#include "../../freeink-sdk/libs/display/FreeInkDisplay/src/driver/Uc8279X4Driver.h"
#undef private
#define private public
#include "../../freeink-sdk/libs/display/FreeInkDisplay/src/driver/Uc8179Driver.h"
#undef private
#include "X4ProGrayScale.h"
#include "X4ProLuts.h"
#include "../../freeink-sdk/libs/display/FreeInkDisplay/src/lut/Uc8279X3Luts.h"

uint8_t mapQualityGray2Level(uint8_t level);
const uint8_t* grayscaleCodeTable();

namespace {

bool g_qualityPass = false;
bool uc8279QualityController() {
  return BoardConfig::ACTIVE.displayController == BoardConfig::DisplayController::UC8279;
}
const BoardConfig::DisplayPins& panelPins() { return BoardConfig::ACTIVE.display; }

constexpr uint8_t kUc8279QualityLutReg[5] = {0x20, 0x21, 0x23, 0x22, 0x24};
#ifndef X4PRO_MIDTONE_LUT_SWAP
#define X4PRO_MIDTONE_LUT_SWAP 0
#endif
#if X4PRO_MIDTONE_LUT_SWAP
constexpr uint8_t kUc8179QualityLutReg[5] = {0x20, 0x21, 0x23, 0x22, 0x24};
#else
constexpr uint8_t kUc8179QualityLutReg[5] = {0x20, 0x21, 0x22, 0x23, 0x24};
#endif

void requestRedriveScrub(EInkDisplay& display) {
  if (BoardConfig::ACTIVE.displayController != BoardConfig::DisplayController::UC8279) return;
  if (display._driver == nullptr) return;
  static_cast<freeink::Uc8279X4Driver*>(display._driver)->_redriveAfterGray = true;
}

bool x4ProBusyWaitSlice(const int8_t busyPin, const uint8_t busyLevel) {
  (void)busyPin;
  (void)busyLevel;
  delay(1);
  return true;
}

EInkDisplay::RefreshMode convertRefreshMode(const HalDisplay::RefreshMode mode) {
  switch (mode) {
    case HalDisplay::FULL_REFRESH:
      return EInkDisplay::FULL_REFRESH;
    case HalDisplay::HALF_REFRESH:
    case HalDisplay::MANUAL_REFRESH:
      // UC8279 stays on FAST deliberately. Its driver's Half branch is a charge scrub
      // that drives every pixel through its complement, which is visible as a full
      // inversion flash - unacceptable for the routine refreshes that come through here.
      // The differential FAST path plus requestRedriveScrub()'s _redriveAfterGray gives
      // the same ghost purge without the flash.
      if (BoardConfig::ACTIVE.displayController == BoardConfig::DisplayController::UC8179) {
        return EInkDisplay::HALF_REFRESH;
      }
      return EInkDisplay::FAST_REFRESH;
    case HalDisplay::STRONG_FAST_REFRESH:
      return EInkDisplay::FAST_REFRESH;
    case HalDisplay::FAST_REFRESH:
    default:
      return EInkDisplay::FAST_REFRESH;
  }
}
}

HalDisplay::HalDisplay()
    : display(panelPins().sclk, panelPins().mosi, panelPins().cs, panelPins().dc, panelPins().rst,
              panelPins().busy) {}

HalDisplay::~HalDisplay() = default;

void HalDisplay::begin() {
  display.setBusyWaitSliceHook(x4ProBusyWaitSlice);
  display.begin();

  {
    const auto ctrl = BoardConfig::ACTIVE.displayController;
    const char* name = "UNKNOWN";
    switch (ctrl) {
      case BoardConfig::DisplayController::SSD1677: name = "SSD1677"; break;
      case BoardConfig::DisplayController::UC8279:  name = "UC8279";  break;
      case BoardConfig::DisplayController::UC8179:  name = "UC8179";  break;
      default: break;
    }
    const bool lutHonoured = (ctrl == BoardConfig::DisplayController::SSD1677);
    INX_SERIAL.printf("[X4PRO][DISPLAY] controller=%s(%d) variant=%u custom_lut=%s\n", name,
                      static_cast<int>(ctrl), BoardConfig::ACTIVE.displayControllerVariant,
                      lutHonoured ? "HONOURED (X4ProLuts)" : "IGNORED by this driver - built-in waveform");
  }

#ifdef X4PRO_GRAY_VARIANT
  forceGrayWaveformVariant(static_cast<uint8_t>(X4PRO_GRAY_VARIANT));
#endif

  const auto wakeupReason = gpio.getWakeupReason();
  if (wakeupReason == HalGPIO::WakeupReason::PowerButton || wakeupReason == HalGPIO::WakeupReason::AfterFlash ||
      wakeupReason == HalGPIO::WakeupReason::Other) {
    display.requestResync();
  }
}

void HalDisplay::clearScreen(const uint8_t color) const { display.clearScreen(color); }

void HalDisplay::drawImage(const uint8_t* imageData, const uint16_t x, const uint16_t y, const uint16_t w,
                           const uint16_t h, const bool fromProgmem) const {
  display.drawImage(imageData, x, y, w, h, fromProgmem);
}

void HalDisplay::displayBuffer(const HalDisplay::RefreshMode mode) {
  if (mode == HALF_REFRESH || mode == MANUAL_REFRESH) requestRedriveScrub(display);
  display.displayBuffer(convertRefreshMode(mode));
}

void HalDisplay::displayBufferAsync(const HalDisplay::RefreshMode mode) {
  display.displayBufferAsync(convertRefreshMode(mode));
}

void HalDisplay::syncWriteBufferFromActive() const { display.syncWriteBufferFromActive(); }

bool HalDisplay::refreshBusy() { return display.refreshBusy(); }

void HalDisplay::refreshDisplay(const HalDisplay::RefreshMode mode, const bool turnOffScreen) {
  if (mode == HALF_REFRESH || mode == MANUAL_REFRESH) requestRedriveScrub(display);
  display.refreshDisplay(convertRefreshMode(mode), turnOffScreen);
}

void HalDisplay::deepSleep() { display.deepSleep(); }

uint8_t* HalDisplay::getFrameBuffer() const { return display.getFrameBuffer(); }

void HalDisplay::copyGrayscaleBuffers(const uint8_t* lsbBuffer, const uint8_t* msbBuffer) {
  display.copyGrayscaleBuffers(lsbBuffer, msbBuffer);
}

// QUALITY ONLY takes the pre-update (freeink-sdk 1e8ee54) plane path, where the driver wrote
// our planes through verbatim (streamPlane, invert=false). 2400379 replaced that with an
// absolute-plane fold - LSB OR-ed into a _grayBase snapshot and streamed INVERTED, MSB
// XOR-ed against it - which re-encodes planes our renderer already finished.
//
// MEDIUM / text AA must NOT take this bypass. Its planes are delta-encoded, and pushing
// them raw sends white through the black-drive waveform bucket, washing the rest of the
// screen gray (the regression ffeaaa2 fixed). Hence the g_qualityPass gate.
void HalDisplay::copyGrayscaleLsbBuffers(const uint8_t* lsbBuffer) {
  // Drain any in-flight B/W refresh BEFORE the planes go in. displayFinish() ends with
  // streamPlane(CMD_DTM1, fb) to re-seed the differential baseline, so if it runs later -
  // and runUc8279Gray()'s syncPendingAsync() is exactly that, after both planes are
  // loaded - it overwrites plane 0 with the B/W frame. Sleep never sees it because
  // nothing is pending there; a book page turn always has an async refresh outstanding.
  display.syncPendingAsync();
  if (g_qualityPass && uc8279QualityController() && display._driver != nullptr && lsbBuffer != nullptr) {
    auto* d = static_cast<freeink::Uc8279X4Driver*>(display._driver);
    d->_absoluteGrayPlanes = false;
    d->_grayBaseValid = false;
    d->streamPlane(display._bus, 0x10, lsbBuffer, false);
    return;
  }
  display.copyGrayscaleLsbBuffers(lsbBuffer);
}

void HalDisplay::copyGrayscaleMsbBuffers(const uint8_t* msbBuffer) {
  if (g_qualityPass && uc8279QualityController() && display._driver != nullptr && msbBuffer != nullptr) {
    auto* d = static_cast<freeink::Uc8279X4Driver*>(display._driver);
    d->_absoluteGrayPlanes = false;
    d->_grayBaseValid = false;
    d->streamPlane(display._bus, 0x13, msbBuffer, false);
    return;
  }
  display.copyGrayscaleMsbBuffers(msbBuffer);
}

void HalDisplay::cleanupGrayscaleBuffers(const uint8_t* bwBuffer) {
  display.cleanupGrayscaleBuffers(bwBuffer);

  if (display._driver == nullptr) return;
  if (BoardConfig::ACTIVE.displayController == BoardConfig::DisplayController::UC8179) {
    static_cast<freeink::Uc8179Driver*>(display._driver)->_redriveAfterGray = false;
  } else if (BoardConfig::ACTIVE.displayController == BoardConfig::DisplayController::UC8279) {
    static_cast<freeink::Uc8279X4Driver*>(display._driver)->_redriveAfterGray = false;
  }
}

namespace {
constexpr uint8_t UC8279_PSR0 = 0x37;
constexpr uint8_t UC8279_PSR1 = 0x4D;
constexpr uint8_t UC8279_CDI_AA = 0x97;

void runUc8279Gray(EInkDisplay& display, const uint8_t tables[5][49], const uint8_t (&lutReg)[5],
                   const char* tag) {
  freeink::EpdBus& bus = display._bus;

  if (display._inverted) return;
  display.syncPendingAsync();

  bus.cmd(0x00);
  bus.data(UC8279_PSR0);
  bus.data(UC8279_PSR1);

  for (uint8_t i = 0; i < 5; ++i) {
    bus.cmd(lutReg[i]);
    bus.data(tables[i], 49);
  }

  bus.cmd(0x50);
  bus.data(UC8279_CDI_AA);

  bus.cmd(0x04);
  bus.waitBusy("x4pro_gray_PON");

  bus.cmd(0x00);
  bus.data(UC8279_PSR0);
  bus.data(UC8279_PSR1);

  bus.cmd(0x12);
  bus.waitBusy(tag);

  display._shadowValid = false;
  display._redRamSynced = false;

  requestRedriveScrub(display);
}

void seedUc8179QualityBase(EInkDisplay& display, bool invertBase = false);

void runUc8179MediumAa(EInkDisplay& display, const char* tag) {
  if (display._driver == nullptr) return;
  if (display._inverted) return;
  auto* driver = static_cast<freeink::Uc8179Driver*>(display._driver);
  freeink::EpdBus& bus = display._bus;

  display.syncPendingAsync();
  display._shadowValid = false;
  display._redRamSynced = false;

  bus.waitBusy("x4pro_8179_aa_ready");
  driver->_bwPlanesSynced = false;

  seedUc8179QualityBase(display);

  bus.cmd(0x00);
  bus.data(driver->_cfg.psr0);
  bus.data(driver->_cfg.psr1);
  for (uint8_t i = 0; i < 5; ++i) {
    bus.cmd(static_cast<uint8_t>(0x20 + i));
    bus.data(inx::x4pro::kUc8179MediumAa[i], inx::x4pro::UC8179_AA_LUT_LEN);
  }
  bus.cmd(0x50);
  bus.data(driver->_cfg.cdiActive);
  bus.data(0x07);
  driver->_grayRefreshedOnce = true;

  if (!driver->_isScreenOn) {
    bus.cmd(0x04);
    bus.waitBusy("x4pro_8179_aa_PON");
    driver->_isScreenOn = true;
  }
  bus.cmd(0x12);
  bus.waitBusy(tag);

  if (driver->_grayBaseValid) {
    driver->streamPlane(bus, 0x10, driver->_grayBase);
    driver->streamPlane(bus, 0x13, driver->_grayBase);
    driver->_oldPlaneValid = true;
    driver->_bwPlanesSynced = true;
    driver->_needFullClear = false;
  }
  driver->_grayBaseValid = false;
  driver->_absoluteGrayPlanes = false;

  driver->_redriveAfterGray = true;
}

void runUc8179QualityGray(EInkDisplay& display, const char* tag) {
  if (display._driver == nullptr) return;

  auto* driver = static_cast<freeink::Uc8179Driver*>(display._driver);
  freeink::EpdBus& bus = display._bus;

  if (display._inverted) return;
  display.syncPendingAsync();

  display._shadowValid = false;
  display._redRamSynced = false;
  driver->_bwPlanesSynced = false;

  bus.waitBusy("x4pro_8179_gray_ready");

  driver->runGrayscalePrecondition(bus);
  bus.cmd(0x00);
  bus.data(driver->_cfg.psr0);
  bus.data(driver->_cfg.psr1);
  for (uint8_t i = 0; i < 5; ++i) {
    bus.cmd(kUc8179QualityLutReg[i]);
    bus.data(inx::x4pro::kUc8179QualityLuts[i].data, inx::x4pro::UC8179_QUALITY_LUT_LEN);
  }

  bus.cmd(0x50);
  bus.data(driver->_cfg.cdiActive);
  bus.data(0x07);
  driver->_grayRefreshedOnce = true;

  if (!driver->_isScreenOn) {
    bus.cmd(0x04);
    bus.waitBusy("x4pro_8179_gray_PON");
    driver->_isScreenOn = true;
  }

  bus.cmd(0x12);
  bus.waitBusy(tag);

  bus.cmd(0x50);
  bus.data(driver->_cfg.cdiIdle);
  bus.data(0x07);

  driver->_oldPlaneValid = false;

}

bool controllerHonoursCustomLut() {
  return BoardConfig::ACTIVE.displayController == BoardConfig::DisplayController::SSD1677;
}

bool controllerUsesUc8279QualityPath() {
  return BoardConfig::ACTIVE.displayController == BoardConfig::DisplayController::UC8279;
}

bool controllerUsesUc8179QualityPath() {
  return BoardConfig::ACTIVE.displayController == BoardConfig::DisplayController::UC8179;
}

void seedUc8179QualityBase(EInkDisplay& display, bool invertBase) {
  if (!controllerUsesUc8179QualityPath() || display._driver == nullptr) return;

  auto* driver = static_cast<freeink::Uc8179Driver*>(display._driver);
  const uint8_t* displayed = display.frameBufferActive != nullptr ? display.frameBufferActive : display.getFrameBuffer();
  if (driver->_grayBase == nullptr || displayed == nullptr) {
    INX_SERIAL.printf("[%lu] [X4PRO][DISPLAY] UC8179 gray base unavailable\n", millis());
    return;
  }

  memcpy(driver->_grayBase, displayed, display.getBufferSize());
  if (invertBase) {
    const size_t n = display.getBufferSize();
    for (size_t i = 0; i < n; ++i) driver->_grayBase[i] = static_cast<uint8_t>(~driver->_grayBase[i]);
  }
  driver->_grayBaseValid = true;
  driver->_absoluteGrayPlanes = false;
}

}

void HalDisplay::displayGrayBuffer(const bool quality, const bool trackForRevert) {
  if (!quality) {
    // Text AA keeps FreeInk's stock table; only images get the custom four-tone banks.
    //
    // Stock kXtfAa02/kXtfAa68 define registers 0x22 and 0x23 byte-identically - three
    // tones, no light grey - and that is not an oversight: it is an ANTI-ALIASING table,
    // where one mid grey per glyph edge is the whole job. Forcing a second grey into it
    // is what left curve edges on o/e/c/s washed out. Images are the case that genuinely
    // needs four separated tones, and they come through here with trackForRevert set.
#ifndef X4PRO_TEXT_AA_STOCK
#define X4PRO_TEXT_AA_STOCK 1
#endif
    if (controllerUsesUc8279QualityPath() && (trackForRevert || !X4PRO_TEXT_AA_STOCK)) {
      static constexpr uint8_t kMediumAaReg[5] = {0x20, 0x21, 0x22, 0x23, 0x24};
      runUc8279Gray(display,
                    BoardConfig::ACTIVE.displayControllerVariant == 0x02
                        ? inx::x4pro::kUc8279MediumAa02
                        : inx::x4pro::kUc8279MediumAa68,
                    kMediumAaReg, trackForRevert ? "x4pro_medium_aa" : "x4pro_text_aa");
      return;
    }
    if (uc8279QualityController()) {
      display.displayGrayBuffer(false, nullptr, false);
      return;
    }
    if (controllerUsesUc8179QualityPath()) {
      runUc8179MediumAa(display, trackForRevert ? "x4pro_8179_medium_aa" : "x4pro_8179_text_aa");
      return;
    }
    display.displayGrayBuffer(false, nullptr, false);
    return;
  }
  if (controllerHonoursCustomLut()) {
    display.displayGrayBuffer(false, inx::x4pro::lut_quality, true);
    return;
  }
  if (controllerUsesUc8279QualityPath()) {
    runUc8279Gray(display, inx::x4pro::scaledGrayBank(), kUc8279QualityLutReg, "x4pro_gray_quality");
    return;
  }
  if (controllerUsesUc8179QualityPath()) {
      runUc8179QualityGray(display, "x4pro_8179_gray_quality");
    return;
  }
  display.displayGrayBuffer(false, nullptr, false);
}

void HalDisplay::displayGrayBufferFastQuality() {
  if (controllerHonoursCustomLut()) {
    display.displayGrayBuffer(false, inx::x4pro::lut_quality_fast, true);
    return;
  }
  if (controllerUsesUc8179QualityPath()) {
    runUc8179QualityGray(display, "x4pro_8179_gray_fastq");
    return;
  }
  runUc8279Gray(display, inx::x4pro::scaledGrayBank(), kUc8279QualityLutReg, "x4pro_gray_fastq");
  g_qualityPass = false;
}

void HalDisplay::forceGrayWaveformVariant(const uint8_t variant) {
  BoardConfig::ACTIVE.displayControllerVariant = variant;
  INX_SERIAL.printf("[X4PRO][DISPLAY] gray AA waveform variant forced to 0x%02X\n", variant);
}

#ifndef X4PRO_UC8179_HQ_INVERT_BASE
#define X4PRO_UC8179_HQ_INVERT_BASE 1
#endif
void HalDisplay::prepareQualityGrayscale() {
  g_qualityPass = true;
  seedUc8179QualityBase(display, X4PRO_UC8179_HQ_INVERT_BASE != 0);
}

uint16_t HalDisplay::getDisplayWidth() const { return display.getDisplayWidth(); }

uint16_t HalDisplay::getDisplayHeight() const { return display.getDisplayHeight(); }

uint16_t HalDisplay::getDisplayWidthBytes() const { return display.getDisplayWidthBytes(); }

uint32_t HalDisplay::getBufferSize() const { return display.getBufferSize(); }
