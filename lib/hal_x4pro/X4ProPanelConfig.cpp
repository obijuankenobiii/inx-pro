/**
 * @file X4ProPanelConfig.cpp
 * @brief Board config override for the UC8279 panel driver.
 *
 * Uc8279X4Driver supports per-board config injection (see the note at the bottom of
 * Uc8279X4Driver.cpp): define a getter in namespace freeink and point
 * -DFREEINK_UC8279_X4_CONFIG at it. That lets the X4 Pro tune panel timing without
 * editing the submodule.
 *
 * WHY: the fast (DU) refresh ghosts noticeably more here than on Sticky or other boards.
 * The lever is TSSET (cmd 0xE5), which FORCES the temperature the controller uses to pick
 * its OTP waveform. Higher forced temperature selects a shorter, weaker waveform; lower
 * selects a longer, stronger one. Stock uses:
 *
 *      tsset      0x1E  (30)  full / GC refresh   -> long, strong
 *      tssetFast  0x5A  (90)  fast / DU refresh   -> very short, and the source of the
 *                                                    leftover previous-frame ghosting
 *
 * 0x5A is an aggressive choice: it buys speed by under-driving every partial update, and
 * the residue accumulates because a DU never fully resolves the pixels it skips. Dropping
 * it lengthens the DU waveform, which is what clears the previous frame.
 *
 * Tune with -DX4PRO_DU_TSSET=<value> (decimal or 0x hex):
 *      0x5A (90)  stock — fastest, most ghosting
 *      0x46 (70)  moderate
 *      0x3C (60)  tried — the DU stops behaving like a partial and flashes black. Not usable.
 *
 * LEFT AT STOCK. Lowering TSSET does not gently strengthen the partial on this panel; it
 * crosses into the flashing waveform. Ghosting is handled instead by mapping HALF_REFRESH
 * onto the driver's no-flash full-pixel re-drive — see HalDisplay.cpp.
 */

#include "../../freeink-sdk/libs/display/FreeInkDisplay/src/driver/Uc8279X4Driver.h"

#ifndef X4PRO_DU_TSSET
#define X4PRO_DU_TSSET 0x5A
#endif

namespace freeink {

const Uc8279X4Config& x4proPanelConfig() {
  static Uc8279X4Config cfg = [] {
    Uc8279X4Config c = uc8279X4DefaultConfig();
    c.tssetFast = static_cast<uint8_t>(X4PRO_DU_TSSET);
    return c;
  }();
  return cfg;
}

}
