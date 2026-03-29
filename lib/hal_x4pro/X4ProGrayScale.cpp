#include "X4ProGrayScale.h"

#include "../../freeink-sdk/libs/display/FreeInkDisplay/src/lut/Uc8279X3Luts.h"

#ifndef X4PRO_GRAY_SPEED
#define X4PRO_GRAY_SPEED 60
#endif

namespace inx {
namespace x4pro {
namespace {

constexpr uint8_t kTables = 5;
constexpr uint8_t kGroups = 7;   // 49 bytes / 7
constexpr uint8_t kPhases = 4;   // bytes 1..4 of each group

struct Phase {
  uint8_t rail;    // 0 GND, 1 VDH, 2 VDL, 3 VDHR
  uint8_t frames;
};

int netCharge(const Phase p[kGroups][kPhases], const uint8_t groups) {
  int n = 0;
  for (uint8_t g = 0; g < groups; ++g)
    for (uint8_t i = 0; i < kPhases; ++i) {
      if (p[g][i].rail == 1) n += p[g][i].frames;
      else if (p[g][i].rail == 2) n -= p[g][i].frames;
    }
  return n;
}

int totalFrames(const Phase p[kGroups][kPhases], const uint8_t groups) {
  int t = 0;
  for (uint8_t g = 0; g < groups; ++g)
    for (uint8_t i = 0; i < kPhases; ++i) t += p[g][i].frames;
  return t;
}

}  // namespace

const uint8_t (*scaledGrayBank())[49] {
  static uint8_t out[kTables][49];
  static bool built = false;
  if (built) return out;

  Phase ph[kTables][kGroups][kPhases] = {};
  uint8_t used[kTables] = {};

  // Decode, scale, and rebalance each table.
  for (uint8_t t = 0; t < kTables; ++t) {
    const uint8_t* src = freeink::kUc8279X3_Xth4[t];
    uint8_t g = 0;
    for (; g < kGroups; ++g) {
      const uint8_t* grp = src + g * 7;
      bool empty = true;
      for (uint8_t i = 0; i < kPhases; ++i)
        if (grp[1 + i] != 0) empty = false;
      if (empty) break;  // terminator group
      for (uint8_t i = 0; i < kPhases; ++i) {
        const uint8_t b = grp[1 + i];
        const uint8_t frames = static_cast<uint8_t>(b & 0x3F);
        uint16_t scaled = (static_cast<uint16_t>(frames) * X4PRO_GRAY_SPEED + 50u) / 100u;
        if (frames != 0 && scaled == 0) scaled = 1;  // never drop a phase entirely
        if (scaled > 63) scaled = 63;                // 6-bit field
        ph[t][g][i] = {static_cast<uint8_t>(b >> 6), static_cast<uint8_t>(scaled)};
      }
    }
    used[t] = g;

    // Rounding perturbs the balance; walk it back to net 0 by growing the longest phase
    // of whichever rail is short.
    int n = netCharge(ph[t], used[t]);
    while (n != 0) {
      const uint8_t want = n > 0 ? 2 : 1;  // too positive -> add VDL
      uint8_t bg = 0xFF, bi = 0, best = 0;
      for (uint8_t g2 = 0; g2 < used[t]; ++g2)
        for (uint8_t i = 0; i < kPhases; ++i)
          if (ph[t][g2][i].rail == want && ph[t][g2][i].frames > best && ph[t][g2][i].frames < 63) {
            best = ph[t][g2][i].frames;
            bg = g2;
            bi = i;
          }
      if (bg == 0xFF) break;  // nothing to adjust; leave as close as we got
      ph[t][bg][bi].frames++;
      n = netCharge(ph[t], used[t]);
    }
  }

  // Pad every table with a GND group so all five span the same frame count.
  int longest = 0;
  for (uint8_t t = 0; t < kTables; ++t) longest = max(longest, totalFrames(ph[t], used[t]));
  for (uint8_t t = 0; t < kTables; ++t) {
    int deficit = longest - totalFrames(ph[t], used[t]);
    while (deficit > 0 && used[t] < kGroups - 1) {
      const uint8_t chunk = static_cast<uint8_t>(deficit > 63 ? 63 : deficit);
      ph[t][used[t]][0] = {0, chunk};
      used[t]++;
      deficit -= chunk;
    }
  }

  // Re-encode. Structural bytes match the OEM tables; the group after the last active one
  // is left all-zero and acts as the terminator.
  for (uint8_t t = 0; t < kTables; ++t) {
    for (uint8_t g = 0; g < used[t]; ++g) {
      uint8_t* dst = out[t] + g * 7;
      dst[0] = 0x01;
      for (uint8_t i = 0; i < kPhases; ++i)
        dst[1 + i] = static_cast<uint8_t>((ph[t][g][i].rail << 6) | (ph[t][g][i].frames & 0x3F));
      dst[5] = 0x01;
      dst[6] = 0x01;
    }
  }

  built = true;
  return out;
}

}  // namespace x4pro
}  // namespace inx
