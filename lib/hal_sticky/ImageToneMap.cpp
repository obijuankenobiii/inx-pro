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

const uint8_t* grayscaleCodeTable() {
  // Sticky's medium waveform orders the panel codes from lightest to darkest
  // as 00, 11, 10, 01.  Image level 1 is dark gray and level 2 is light gray,
  // so the two middle codes must be assigned in the reverse order.
  static constexpr uint8_t kTable[4] = {
      0b00,
      0b11,
      0b10,
      0b01,
  };
  return kTable;
}

uint8_t mapQualityGray2Level(const uint8_t level) {
  const uint8_t l = level & 3u;
  if (l == 1u) return 2u;
  if (l == 2u) return 1u;
  return l;
}

const uint8_t* mediumTextCodeTable() { return grayscaleCodeTable(); }

int applyDeviceToneCurve(const int gray) { return gray; }
