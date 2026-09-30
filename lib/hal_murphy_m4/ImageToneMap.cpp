/**
 * @file ImageToneMap.cpp
 * @brief Murphy M4 image tone mapping.
 *
 * Murphy's SSD1677 uses the same four-tone code ordering as the existing
 * Murphy-compatible display path. Keeping the mapping in the Murphy HAL
 * prevents X4 Pro tuning from leaking into this target.
 */

#include <cstdint>

#ifndef MURPHY_M4_IMAGE_CONTRAST
#define MURPHY_M4_IMAGE_CONTRAST 145
#endif
#ifndef MURPHY_M4_IMAGE_DARKEN
#define MURPHY_M4_IMAGE_DARKEN 12
#endif

const uint8_t* grayscaleCodeTable() {
  static constexpr uint8_t kMurphySsd1677[4] = {
      0b11,
      0b01,
      0b10,
      0b00,
  };
  return kMurphySsd1677;
}

uint8_t mapQualityGray2Level(const uint8_t level) {
  const uint8_t l = level & 3u;
  if (l == 1u) return 2u;
  if (l == 2u) return 1u;
  return l;
}

const uint8_t* mediumTextCodeTable() { return grayscaleCodeTable(); }

int applyDeviceToneCurve(const int gray) {
  int g = ((gray - 128) * MURPHY_M4_IMAGE_CONTRAST) / 100 + 128;
  g -= MURPHY_M4_IMAGE_DARKEN;
  if (g < 0) g = 0;
  if (g > 255) g = 255;
  return g;
}

