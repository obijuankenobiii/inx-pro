/**
 * @file ImageToneMap.cpp
 * @brief Seeed reTerminal Sticky image tone mapping.
 *
 * Both tables are exactly the values that lived in lib/GfxRenderer before they were made
 * per-device — Sticky's rendering is unchanged. The X4 Pro's live in
 * lib/hal_x4pro/ImageToneMap.cpp; the two are separate translation units so a tone fix on
 * one board cannot alter the other.
 */

#include <cstdint>

// On-panel brightness order for the medium (no-flicker) waveform is
// 00 (lightest) -> 11 -> 10 -> 01 (darkest); the entries render inverse to their
// drive-bit labels. Image tones are mapped onto that real order.
const uint8_t* grayscaleCodeTable() {
  static constexpr uint8_t kTable[4] = {
      0b00,  // level 0  white      -> lightest
      0b10,  // level 1  dark gray
      0b11,  // level 2  light gray
      0b01,  // level 3  black      -> darkest
  };
  return kTable;
}

// Quality (GRAY2) path: the two middle levels are swapped relative to their codes.
uint8_t mapQualityGray2Level(const uint8_t level) {
  const uint8_t l = level & 3u;
  if (l == 1u) return 2u;
  if (l == 2u) return 1u;
  return l;
}

// Sticky's text AA uses the same codes as its images.
const uint8_t* mediumTextCodeTable() { return grayscaleCodeTable(); }

// Sticky needs no tone correction — its image rendering is unchanged.
int applyDeviceToneCurve(const int gray) { return gray; }
