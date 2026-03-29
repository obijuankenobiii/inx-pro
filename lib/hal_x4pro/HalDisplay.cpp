/**
 * @file HalDisplay.cpp
 * @brief Xteink X4 Pro HalDisplay — thin passthrough to freeink-sdk's FreeInkDisplay.
 *
 * See HalDisplay.h for why this does NOT reuse the Sticky display wrapper.
 */

#include <BoardConfig.h>
#include <XteinkDetect.h>
#include <esp_idf_version.h>

// Uc8279X4Driver::displayGray() hardcodes `(void)lut;` — the SDK offers NO route to
// upload a custom waveform on this controller. Driving it therefore needs EpdBus,
// which FreeInkDisplay keeps private. The submodule is kept pristine, so reach it the
// same way the Sticky wrapper does for its own board waveform work. Scope is one member.
#define private public
#include <HalDisplay.h>
#undef private
#include <HalGPIO.h>

// Same `#define private public` reach-through as HalDisplay above. runUc8279Gray() fires
// DRF itself, so the driver never runs its own displayGray() and never sets the post-gray
// scrub flag it relies on — see the note where that flag is set.
#define private public
#include "../../freeink-sdk/libs/display/FreeInkDisplay/src/driver/Uc8279X4Driver.h"
#undef private
#define private public
#include "../../freeink-sdk/libs/display/FreeInkDisplay/src/driver/Uc8179Driver.h"
#undef private
#include "X4ProGrayScale.h"
#include "X4ProLuts.h"
// OEM UC8279d 4-level grayscale bank, reverse-engineered from stock Xteink firmware
// and working on X3 hardware. Its own header states the table format matches the X4 Pro
// vendor reference (5 x 49 bytes, raw, register sent separately), and unlike the X4 Pro's
// XtfAa it is a REAL grayscale waveform: 71 frames, four distinct push-pull channels,
// every table DC-balanced at net 0.
#include "../../freeink-sdk/libs/display/FreeInkDisplay/src/lut/Uc8279X3Luts.h"

// ImageToneMap.cpp intentionally has no public header; these declarations keep the
// debug report tied to the exact mapping used by the renderer without changing that
// existing interface.
uint8_t mapQualityGray2Level(uint8_t level);
const uint8_t* grayscaleCodeTable();

namespace {
const BoardConfig::DisplayPins& panelPins() { return BoardConfig::ACTIVE.display; }

// UC8279's X4 quality bank uses the established X4 channel exchange. Keep that
// order local to the UC8279 path; UC8179 uses the controller's own register order.
constexpr uint8_t kUc8279QualityLutReg[5] = {0x20, 0x21, 0x23, 0x22, 0x24};
// X4Pro UC8179 quality order. Keep the two middle transition channels exchanged;
// this is the known-working panel order. The color correction belongs in
// ImageToneMap.cpp, not in the waveform upload sequence.
// Register order for the quality LUT upload, indexed the same as kUc8179QualityLuts
// (whose payloads are ordered VCOM, white, light, dark, black).
//
// Uc8179Driver::copyGrayscaleLsb documents this panel's absolute selector convention as
// (plane0/DTM1, plane1/DTM2):  black=(0,0), dark=(1,0), light=(0,1), white=(1,1).
// DTM1 is the old plane and DTM2 the new one, so that resolves to
//   white (1,1) -> LUTWW 0x21    light (0,1) -> LUTBW 0x22
//   dark  (1,0) -> LUTWB 0x23    black (0,0) -> LUTBB 0x24
// i.e. plain ascending order, which already matches the payload order.
//
// This array previously exchanged the two middle registers, which sent light grey's
// waveform to the dark channel and vice versa - the extremes stayed roughly right while
// every midtone inverted, so images lost all modelling and read as flat and muddy.
//
// Set -DX4PRO_MIDTONE_LUT_SWAP=1 to restore the old exchanged order and A/B it on hardware.
#ifndef X4PRO_MIDTONE_LUT_SWAP
#define X4PRO_MIDTONE_LUT_SWAP 0
#endif
#if X4PRO_MIDTONE_LUT_SWAP
constexpr uint8_t kUc8179QualityLutReg[5] = {0x20, 0x21, 0x23, 0x22, 0x24};
#else
constexpr uint8_t kUc8179QualityLutReg[5] = {0x20, 0x21, 0x22, 0x23, 0x24};
#endif

// Ask the driver to re-drive EVERY pixel on the next refresh. On the fast path this
// rewrites the OLD plane as the complement of the target, so every pixel classifies as
// changed and is driven to its endpoint — optically invisible where a pixel is already
// correct, but it scrubs residue. The driver's own words: "no GC flash". This is what the
// post-grayscale path uses, and it is the only full-pixel refresh this controller offers
// that does not flash black.
void requestRedriveScrub(EInkDisplay& display) {
  // This flag exists only in the UC8279 X4 driver. X4 Pro units can also probe
  // as UC8179; casting that active driver here would overwrite its refresh state.
  if (BoardConfig::ACTIVE.displayController != BoardConfig::DisplayController::UC8279) return;
  if (display._driver == nullptr) return;
  static_cast<freeink::Uc8279X4Driver*>(display._driver)->_redriveAfterGray = true;
}

// Arduino-ESP32 3.3.x routes attachInterrupt() through the core-0 IPC task.
// On the X4 Pro that task overflows while EpdBus arms the SSD1677 BUSY edge
// interrupt. Use the SDK's bounded polling path instead; it preserves the
// refresh waveform and avoids the IPC stack failure.
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
      // UC8179 honours Half for real: Uc8179Driver::displayStart computes
      // fast = (mode == Fast) && ..., so Half takes the full-drive path and clears ghosting.
      // Collapsing it to Fast here left HQ image pages ghosting into the next page, because
      // requestRedriveScrub() is a no-op on this controller - so "half" became a plain diff
      // with nothing armed to scrub it. The collapse below is a UC8279 workaround only.
      if (BoardConfig::ACTIVE.displayController == BoardConfig::DisplayController::UC8179) {
        return EInkDisplay::HALF_REFRESH;
      }
      // Uc8279X4Driver treats Half exactly like Fast (fast = mode != Full), so asking for
      // HALF_REFRESH here would give a plain weak DU and the caller's intent — "clear the
      // ghosting without a black flash" — would be lost. displayBuffer()/refreshDisplay()
      // arm the re-drive scrub before calling through, which is the real Half on this
      // panel; the mode itself stays Fast.
      return EInkDisplay::FAST_REFRESH;
    case HalDisplay::STRONG_FAST_REFRESH:
      // freeink-sdk's RefreshMode has no STRONG_FAST_REFRESH tier (only
      // FULL/HALF/FAST) — collapses to FAST_REFRESH.
      return EInkDisplay::FAST_REFRESH;
    case HalDisplay::FAST_REFRESH:
    default:
      return EInkDisplay::FAST_REFRESH;
  }
}
}  // namespace

HalDisplay::HalDisplay()
    : display(panelPins().sclk, panelPins().mosi, panelPins().cs, panelPins().dc, panelPins().rst,
              panelPins().busy) {}

HalDisplay::~HalDisplay() = default;

void HalDisplay::begin() {
  // No SPI.begin() here on purpose — see the header. EpdBus::begin() brings the
  // panel bus up from BoardConfig::ACTIVE.display, and the SSD1677 driver reports
  // spiMiso() == -1, so nothing binds the SDMMC data lines into the SPI peripheral.
  display.setBusyWaitSliceHook(x4ProBusyWaitSlice);
  display.begin();

  // Report which panel silicon the boot probe actually selected. This matters for
  // grayscale: the X4 Pro ships with more than one controller, and only the SSD1677
  // driver honours a custom 110-byte LUT. Uc8279X4Driver::displayGray and
  // Uc8179Driver::displayGray both do `(void)lut;` and run their own built-in
  // waveform tables instead (5 short LUTs uploaded via separate commands - a
  // completely different format), so on those units X4ProLuts.h is silently
  // discarded and grayscale falls back to plain plane output.
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
  // Build-time override of the OEM AA grayscale table (see forceGrayWaveformVariant).
  // Applied after display.begin() so it lands on top of whatever the boot probe chose.
  forceGrayWaveformVariant(static_cast<uint8_t>(X4PRO_GRAY_VARIANT));
#endif

  // Force a clean, non-differential first paint on boot/wake so stale panel content
  // (or the stock Xteink sleep screen) doesn't ghost through the first FAST_REFRESH.
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

// Synchronous: the SDK owns the BUSY wait. Sticky's async-then-poll-BUSY path is a
// workaround for a Sticky-only ISR-arming race and is not reproduced here.
void HalDisplay::displayBuffer(const HalDisplay::RefreshMode mode) {
  if (mode == HALF_REFRESH || mode == MANUAL_REFRESH) requestRedriveScrub(display);
  display.displayBuffer(convertRefreshMode(mode));
}

void HalDisplay::displayBufferAsync(const HalDisplay::RefreshMode mode) {
  // The X4 Pro driver must finish the panel waveform before the activity can
  // continue rendering or polling input. Keep the shared async API synchronous
  // on this HAL; the panel BUSY wait already uses the X4 Pro polling hook.
  display.displayBuffer(convertRefreshMode(mode));
}

// X4 Pro also uses the SDK's dual host framebuffers. Temporary overlays and
// progress widgets must start from the currently displayed frame, just like
// Sticky, before changing only a small region.
void HalDisplay::syncWriteBufferFromActive() const { display.syncWriteBufferFromActive(); }

void HalDisplay::refreshDisplay(const HalDisplay::RefreshMode mode, const bool turnOffScreen) {
  if (mode == HALF_REFRESH || mode == MANUAL_REFRESH) requestRedriveScrub(display);
  display.refreshDisplay(convertRefreshMode(mode), turnOffScreen);
}

void HalDisplay::deepSleep() { display.deepSleep(); }

uint8_t* HalDisplay::getFrameBuffer() const { return display.getFrameBuffer(); }

void HalDisplay::copyGrayscaleBuffers(const uint8_t* lsbBuffer, const uint8_t* msbBuffer) {
  display.copyGrayscaleBuffers(lsbBuffer, msbBuffer);
}

void HalDisplay::copyGrayscaleLsbBuffers(const uint8_t* lsbBuffer) { display.copyGrayscaleLsbBuffers(lsbBuffer); }

void HalDisplay::copyGrayscaleMsbBuffers(const uint8_t* msbBuffer) { display.copyGrayscaleMsbBuffers(msbBuffer); }

void HalDisplay::cleanupGrayscaleBuffers(const uint8_t* bwBuffer) {
  display.cleanupGrayscaleBuffers(bwBuffer);

  // Cleanup has restored the B/W baseline. Clear the one-shot post-gray scrub
  // request in the wrapper so the next B/W paint does not run a second full
  // panel transition and leave dirty ink behind. Keep both driver layouts
  // guarded because X4 Pro batches can probe as UC8179 or UC8279.
  if (display._driver == nullptr) return;
  if (BoardConfig::ACTIVE.displayController == BoardConfig::DisplayController::UC8179) {
    static_cast<freeink::Uc8179Driver*>(display._driver)->_redriveAfterGray = false;
  } else if (BoardConfig::ACTIVE.displayController == BoardConfig::DisplayController::UC8279) {
    static_cast<freeink::Uc8279X4Driver*>(display._driver)->_redriveAfterGray = false;
  }
}

// Grayscale image rendering.
//
// The X4 Pro ships with more than one panel controller and the boot probe picks the
// driver at runtime, so this has to branch on what was actually detected:
//
//   SSD1677  -> honours a custom 110-byte LUT via setCustomLut(). Use X4ProLuts.h.
//   UC8279   -> Uc8279X4Driver::displayGray does `(void)lut;` and runs its own
//   UC8179      built-in OEM tables (5 x 49 bytes, a different format entirely).
//               Passing a LUT there is silently discarded.
//
// For the UltraChip parts the quality tier is therefore NOT a new waveform. It is the
// OEM AA waveform run twice over the same planes. That is deliberate: an e-ink waveform
// must be DC-balanced (net zero charge per pixel) or the panel degrades permanently,
// and applying a balanced waveform N times is still balanced - whereas hand-editing
// phase durations to build a "nicer" table is exactly how you lose that guarantee.
// A second pass drives partially-developed pixels further toward their target rail,
// which is what deepens blacks and separates the mid-greys.
namespace {
// Stock AA sequence from Uc8279X4Driver::displayGray, with our tables substituted for
// the built-in ones: PSR(REG=1, external LUT) -> 5 LUT uploads -> CDI -> PON -> PSR
// rewrite (PON reloads MTP, so only a post-PON PSR latches) -> DRF.
// Planes must already be in RAM — the caller runs copyGrayscale*() first.
constexpr uint8_t UC8279_PSR0 = 0x37;
constexpr uint8_t UC8279_PSR1 = 0x4D;
constexpr uint8_t UC8279_CDI_AA = 0x97;

void runUc8279Gray(EInkDisplay& display, const uint8_t tables[5][49], const uint8_t (&lutReg)[5],
                   const char* tag) {
  freeink::EpdBus& bus = display._bus;

  // FreeInkDisplay::displayGrayBuffer() returns early when inverted: that mode deliberately
  // renders a crisp B/W page, and writing grayscale planes would partially undo it. These
  // wrappers drive the bus directly, so they have to honour it themselves.
  if (display._inverted) return;
  // FreeInkDisplay::displayGrayBuffer() drains any deferred refresh before it touches the
  // panel. This wrapper drives the bus directly, so it has to do the same - otherwise a
  // pending async refresh races the LUT upload and DRF, and the planes are driven from
  // stale RAM.
  display.syncPendingAsync();

  bus.cmd(0x00);  // PSR — REG=1, external LUT
  bus.data(UC8279_PSR0);
  bus.data(UC8279_PSR1);

  // Channel -> register mapping. The four grey channels are the 2-bit plane value's
  // four states; which physical tone each produces is decided purely by WHICH register
  // its table lands in. Straight order (0x20..0x24) put the two mid-tones the wrong way
  // round on this panel - dark grey rendered where light grey belonged and vice versa -
  // so BW and WB are swapped here. Table 0 (VCOM) and the two extremes are unaffected.
  //   index 0 -> 0x20 C (VCOM)
  //   index 1 -> 0x21 WW   (lightest)
  //   index 2 -> 0x23 WB   <-- swapped
  //   index 3 -> 0x22 BW   <-- swapped
  //   index 4 -> 0x24 BB   (darkest)
  // If the mid-tones ever look inverted again, this array is the single thing to flip.
  for (uint8_t i = 0; i < 5; ++i) {
    bus.cmd(lutReg[i]);
    bus.data(tables[i], 49);
  }

  bus.cmd(0x50);  // CDI
  bus.data(UC8279_CDI_AA);

  bus.cmd(0x04);  // PON
  bus.waitBusy("x4pro_gray_PON");

  bus.cmd(0x00);  // PSR rewrite — PON reloads MTP, only a post-PON write latches
  bus.data(UC8279_PSR0);
  bus.data(UC8279_PSR1);

  bus.cmd(0x12);  // DRF
  bus.waitBusy(tag);

  display._shadowValid = false;
  display._redRamSynced = false;

  // Post-gray scrub flag. Uc8279X4Driver::displayGray() ends with _redriveAfterGray = true,
  // which makes the NEXT refresh rewrite the OLD plane as the complement of the target so
  // every pixel is re-driven — that is what clears the grey residue an AA/grayscale pass
  // leaves behind. Because this function drives DRF directly, the driver never runs that
  // path and never sets the flag, so the residue survives into the following text-AA pass
  // and shows up as grey bleeding into the image that was just rendered.
  //
  // Set it on the driver ourselves. Guarded by the controller check at the call sites, so
  // the cast only happens when the boot probe actually selected the UC8279 driver.
  requestRedriveScrub(display);
}

// Medium / text-AA override for UC8179, the sibling of runUc8279Gray. Replicates
// Uc8179Driver::displayGray's stock stream exactly - PSR 0x3F -> five 42-byte LUTs -> CDI
// cdiActive/0x07 -> PON -> DRF -> B/W base restore -> _redriveAfterGray - and substitutes
// only the LUT bank, so the two midtones stop being identical.
void seedUc8179QualityBase(EInkDisplay& display, bool invertBase = false);

void runUc8179MediumAa(EInkDisplay& display, const char* tag) {
  if (display._driver == nullptr) return;
  // FreeInkDisplay::displayGrayBuffer() returns early when inverted: that mode deliberately
  // renders a crisp B/W page, and writing grayscale planes would partially undo it. These
  // wrappers drive the bus directly, so they have to honour it themselves.
  if (display._inverted) return;
  auto* driver = static_cast<freeink::Uc8179Driver*>(display._driver);
  freeink::EpdBus& bus = display._bus;

  display.syncPendingAsync();  // as FreeInkDisplay::displayGrayBuffer does
  display._shadowValid = false;
  display._redRamSynced = false;

  bus.waitBusy("x4pro_8179_aa_ready");
  driver->_bwPlanesSynced = false;

  // ARM THE SCRUB. Uc8179Driver's post-AA clear is transitionGrayscaleBase(), and display()
  // only reaches it when ALL of these hold:
  //     mode == Fast && _redriveAfterGray && _grayRefreshedOnce && _oldPlaneValid
  //     && !_needFullClear
  // The last two are set only by the _grayBaseValid restore after DRF below - and on the
  // MEDIUM path nothing seeds _grayBase (prepareQualityGrayscale() runs only when quality),
  // so that block was skipped, DTM1 kept the gray plane instead of a clean B/W base, and the
  // scrub never armed. That is why greys survived the page turn and the book exit.
  //
  // Seed it here from the frame currently on the panel. The planes are already in controller
  // RAM by this point, so this only supplies a clean base for the restore - it does not
  // change how any pixel is driven.
  //
  // Note this differs from UC8279, where the flag alone is enough: Uc8279X4Driver consumes it
  // unconditionally (_darkBackground || _redriveAfterGray) with no mode or plane guards.
  seedUc8179QualityBase(display);

  bus.cmd(0x00);  // PSR - 0x3F: REG=1 custom LUT + KW + SHL
  bus.data(driver->_cfg.psr0);
  bus.data(driver->_cfg.psr1);
  for (uint8_t i = 0; i < 5; ++i) {
    bus.cmd(static_cast<uint8_t>(0x20 + i));
    bus.data(inx::x4pro::kUc8179MediumAa[i], inx::x4pro::UC8179_AA_LUT_LEN);
  }
  bus.cmd(0x50);  // CDI - stock AA value, NOT the quality path's 0x10
  bus.data(driver->_cfg.cdiActive);
  bus.data(0x07);
  driver->_grayRefreshedOnce = true;

  if (!driver->_isScreenOn) {
    bus.cmd(0x04);  // PON
    bus.waitBusy("x4pro_8179_aa_PON");
    driver->_isScreenOn = true;
  }
  bus.cmd(0x12);  // DRF
  bus.waitBusy(tag);

  // Stock writes the clean B/W base to BOTH planes after DRF: it preserves the next
  // transition's old frame and stops a stale gray selector plane being reused later.
  if (driver->_grayBaseValid) {
    driver->streamPlane(bus, 0x10, driver->_grayBase);
    driver->streamPlane(bus, 0x13, driver->_grayBase);
    driver->_oldPlaneValid = true;
    driver->_bwPlanesSynced = true;
    driver->_needFullClear = false;
  }
  driver->_grayBaseValid = false;
  driver->_absoluteGrayPlanes = false;

  // THE CLEAR. Uc8179Driver::displayGray ends with exactly this. Omitting it is why the page
  // did not clear on the next turn or on leaving the book: this flag makes the following B/W
  // refresh re-drive every pixel to its target, which is the only thing that neutralises the
  // intermediate charge an AA pass leaves. A plain DU diff cannot - the B/W baseline records
  // those pixels as already correct, so the residue survives.
  driver->_redriveAfterGray = true;
}

void runUc8179QualityGray(EInkDisplay& display, const char* tag) {
  if (display._driver == nullptr) return;

  auto* driver = static_cast<freeink::Uc8179Driver*>(display._driver);
  freeink::EpdBus& bus = display._bus;

  if (display._inverted) return;  // as FreeInkDisplay::displayGrayBuffer does
  display.syncPendingAsync();

  // This is the UC8179 quality path only. The plane buffers have already been
  // copied by FreeInkDisplay; this wrapper replaces only the LUT upload and
  // activation sequence, leaving the vendored driver's normal AA path intact.
  display._shadowValid = false;
  display._redRamSynced = false;
  driver->_bwPlanesSynced = false;

  bus.waitBusy("x4pro_8179_gray_ready");

  // OEM XTF_PRE_BW_MID conditioning pass, which stock runs before every gray waveform. The
  // SDK states its purpose directly: it runs over equal B/W planes "so gray and white particle
  // states do not relax after the page stops updating". Skipping it is why the panel settles
  // to grey once the refresh finishes - the tones are driven correctly and then relax.
  //
  // Self-guarded: it returns immediately unless _oldPlaneValid and _grayRefreshedOnce hold.
  driver->runGrayscalePrecondition(bus);
  bus.cmd(0x00);  // PSR: custom LUT mode, KW, and the board's orientation bit.
  bus.data(driver->_cfg.psr0);
  bus.data(driver->_cfg.psr1);
  // Upload in kUc8179QualityLutReg order; see the derivation of that order above.
  for (uint8_t i = 0; i < 5; ++i) {
    bus.cmd(kUc8179QualityLutReg[i]);
    bus.data(inx::x4pro::kUc8179QualityLuts[i].data, inx::x4pro::UC8179_QUALITY_LUT_LEN);
  }

  bus.cmd(0x50);  // VCOM data interval.
  // Use the SAME CDI as the medium path (_cfg.cdiActive, stock 0x29), which is confirmed to
  // resolve four tones on this panel. This previously sent UC8179_QUALITY_CDI (0x10) on the
  // theory that the 4-gray LUT set requires a gray-mode value - that claim was never verified
  // here, and it shipped alongside a waveform that did not work, so it cannot be trusted as
  // the reason anything did or did not render. One unverified variable at a time.
  bus.data(driver->_cfg.cdiActive);
  bus.data(0x07);
  driver->_grayRefreshedOnce = true;

  if (!driver->_isScreenOn) {
    bus.cmd(0x04);  // PON
    bus.waitBusy("x4pro_8179_gray_PON");
    driver->_isScreenOn = true;
  }

  bus.cmd(0x12);  // DRF
  bus.waitBusy(tag);

  // Restore the idle CDI (border), exactly as Uc8179Driver::displayFinish does after every
  // refresh. cdiIdle (0xA9) is the border setting; leaving the panel on cdiActive (0x29) after
  // the refresh is what makes it settle to grey instead of ending on black or white. This
  // wrapper drives DRF itself, so displayFinish never runs and never restores it.
  bus.cmd(0x50);
  bus.data(driver->_cfg.cdiIdle);
  bus.data(0x07);

  // The panel now shows a four-level image. No one-bit plane represents it, so the driver must
  // not diff against one: Uc8179Driver::displayStart takes its fast route on
  // (mode == Fast && !_needFullClear && _oldPlaneValid) and would drive only the pixels it
  // believes changed, leaving the rest of the image behind as ghosting.
  //
  // Say so here rather than having callers pick a refresh mode by controller. Recent -> book ->
  // close ghosted only because the exit's FAST diff ran against a plane that was never synced;
  // going to the next page first hid it, since that page turn re-synced the plane.
  driver->_oldPlaneValid = false;

  // Restore the clean B/W base in both controller planes, exactly as the native
  // driver does after AA, so the next page has a valid differential baseline.
  // High quality is a SEPARATE path. It renders a complete four-level image, so it wants none
  // of the AA overlay's plane bookkeeping: no B/W base restore, no _oldPlaneValid /
  // _bwPlanesSynced / _needFullClear, no redrive arming. Those exist because the overlay leaves
  // a B/W page on the panel that the next refresh diffs against; this path does not.
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

  // The grayscale renderer supplies overlay masks, while the UC8179 native
  // driver needs the actual B/W page currently on the panel to rebuild
  // absolute gray selectors. Seed the base without another panel refresh.
  memcpy(driver->_grayBase, displayed, display.getBufferSize());
  if (invertBase) {
    // The QUALITY path folds this base into the selector planes:
    //     plane0 = base | maskLsb      plane1 = plane0 ^ maskMsb
    // and white/black both map to mask 0b00 (mapQualityGray2Level {0,3,2,0}), so the BASE
    // alone decides which of them a pixel becomes. With the wrong polarity white takes
    // (0,0) -> black and black takes (1,1) -> white: the render comes out a clean negative,
    // which is exactly what the panel showed. The mapping cannot cause that - it has no
    // white/black distinction to get wrong.
    //
    // The MEDIUM wrapper passes invertBase=false: it does not fold, and uses this buffer
    // only to restore the B/W planes after DRF, where the un-inverted frame is correct.
    const size_t n = display.getBufferSize();
    for (size_t i = 0; i < n; ++i) driver->_grayBase[i] = static_cast<uint8_t>(~driver->_grayBase[i]);
  }
  driver->_grayBaseValid = true;
  driver->_absoluteGrayPlanes = false;
}

}  // namespace

void HalDisplay::displayGrayBuffer(const bool quality, const bool trackForRevert) {
  if (!quality) {
    // Medium tier - text antialiasing and medium-quality images.
    //
    // The stock UC8279 AA table set defines registers 0x22 and 0x23 identically, so it
    // resolves only three tones: white, one grey, black. Light grey had nowhere to land and
    // came out as ordinary grey. Upload a bank whose 0x22 (light grey) drives one frame
    // instead of the dark channel's three; see the note on kUc8279MediumAa* in X4ProLuts.h.
    //
    // This MUST go through runUc8279Gray(), not a bare DRF: the driver's displayGray() ends
    // with _redriveAfterGray = true, and that scrub is the only thing that clears the grey
    // residue an AA pass leaves behind. runUc8279Gray() calls requestRedriveScrub() to set
    // it, which is what makes this override safe where an earlier bypass attempt was not.
    //
    // Do NOT enlarge the intermediate drives here. X4ProGrayScale.h records that larger
    // one-way drives make every AA pass push those pixels further toward black with nothing
    // pulling them back, so ghosting accumulates pass after pass.
    // Text AA takes this same path. It is the same 2-bit data on the same levels through
    // the same planes, and mediumTextCodeForLevel() now returns the same per-device table,
    // so there is no reason for it to run on a different bank. Keeping it on the stock bank
    // is what left its light edge on the unseparated mid grey.
    //
    // Set -DX4PRO_TEXT_AA_STOCK=1 to send text AA back to the stock driver path.
#ifndef X4PRO_TEXT_AA_STOCK
#define X4PRO_TEXT_AA_STOCK 0
#endif
#if X4PRO_TEXT_AA_STOCK
    if (controllerUsesUc8279QualityPath() && trackForRevert) {
      // Ascending: the stock driver's own upload order, so each bank row lands in the
      // register it is named for.
      static constexpr uint8_t kMediumAaReg[5] = {0x20, 0x21, 0x22, 0x23, 0x24};
      runUc8279Gray(display,
                    BoardConfig::ACTIVE.displayControllerVariant == 0x02
                        ? inx::x4pro::kUc8279MediumAa02
                        : inx::x4pro::kUc8279MediumAa68,
                    kMediumAaReg, "x4pro_medium_aa");
      return;
    }
#else
    if (controllerUsesUc8279QualityPath()) {
      static constexpr uint8_t kMediumAaReg2[5] = {0x20, 0x21, 0x22, 0x23, 0x24};
      runUc8279Gray(display,
                    BoardConfig::ACTIVE.displayControllerVariant == 0x02
                        ? inx::x4pro::kUc8279MediumAa02
                        : inx::x4pro::kUc8279MediumAa68,
                    kMediumAaReg2, trackForRevert ? "x4pro_medium_aa" : "x4pro_text_aa");
      return;
    }
#endif
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
    // UC8279 X4 Pro only: this is the separate X4 UC8279 quality path.
    runUc8279Gray(display, inx::x4pro::scaledGrayBank(), kUc8279QualityLutReg, "x4pro_gray_quality");
    return;
  }
  if (controllerUsesUc8179QualityPath()) {
    // UC8179 quality is X4 Pro board policy. Upload the full quality LUT from
    // this wrapper; the vendored driver remains on its native medium-AA LUT.
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
  // Fast-quality tier — the same cheap 4-level AA pass as medium.
  // Fast-quality tier. This IS a high-quality tier, not a medium one:
  // renderGrayscalePasses() only reaches it when quality==true (useFastQuality =
  // quality && fastQuality), and EPUB book images route here via
  // kReaderHighQualityFastLut. So it gets the Xth4 bank, same as displayGrayBuffer(true).
  // Leaving it on the stock path made high quality silently do nothing on book images
  // while still working on the sleep screen, which passes fastQuality=false.
  if (controllerUsesUc8179QualityPath()) {
    runUc8179QualityGray(display, "x4pro_8179_gray_fastq");
    return;
  }
  runUc8279Gray(display, inx::x4pro::scaledGrayBank(), kUc8279QualityLutReg, "x4pro_gray_fastq");
}

// A/B the two OEM AA table sets on this unit. selectAaLuts() reads
// BoardConfig::ACTIVE.displayControllerVariant on every gray refresh, and ACTIVE is
// mutable, so forcing it here swaps waveform without touching the pristine submodule.
// 0x02 and 0x68 are both OEM bytes; they differ only in the third/fourth frame-group
// values. If the boot probe guessed wrong for your panel, this is the lever.
void HalDisplay::forceGrayWaveformVariant(const uint8_t variant) {
  BoardConfig::ACTIVE.displayControllerVariant = variant;
  INX_SERIAL.printf("[X4PRO][DISPLAY] gray AA waveform variant forced to 0x%02X\n", variant);
}

#ifndef X4PRO_UC8179_HQ_INVERT_BASE
#define X4PRO_UC8179_HQ_INVERT_BASE 1
#endif
void HalDisplay::prepareQualityGrayscale() {
  // Quality folds the base into the planes - see the note in seedUc8179QualityBase().
  // Build with -DX4PRO_UC8179_HQ_INVERT_BASE=0 if the render comes out inverted the other way.
  seedUc8179QualityBase(display, X4PRO_UC8179_HQ_INVERT_BASE != 0);
}

uint16_t HalDisplay::getDisplayWidth() const { return display.getDisplayWidth(); }

uint16_t HalDisplay::getDisplayHeight() const { return display.getDisplayHeight(); }

uint16_t HalDisplay::getDisplayWidthBytes() const { return display.getDisplayWidthBytes(); }

uint32_t HalDisplay::getBufferSize() const { return display.getBufferSize(); }
