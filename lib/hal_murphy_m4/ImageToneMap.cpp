/**
 * @file ImageToneMap.cpp
 * @brief Murphy M4 image tone mapping.
 *
 * Murphy's SSD1677 uses the same four-tone code ordering as the existing
 * Murphy-compatible display path. Keeping the mapping in the Murphy HAL
 * prevents X4 Pro tuning from leaking into this target.
 */

#include <cstdint>

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

// Murphy's quality image path uses the same neutral tone mapping as Sticky.
// The previous contrast boost plus darkening crushed image midtones before the
// four-level waveform ran, making covers appear unnecessarily dark.
int applyDeviceToneCurve(const int gray) { return gray; }
