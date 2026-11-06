/**
 * @file ImageToneMap.cpp
 * @brief LilyGo T5 S3 image tone mapping.
 *
 * freeink-sdk's LgfxEpdDriver does not send plane codes to a controller. It overlays the
 * two 1-bpp planes on the 8-bit canvas left by the last B/W push (overlayCanvasGray()):
 *
 *     LSB  MSB   canvas pixel
 *      0    0    unchanged - keeps the B/W base (white, or black where the base drew ink)
 *      1    x    dark grey  (0x55)
 *      0    1    light grey (0xAA)
 *
 * So the codes here are absolute, and white and black must both be 0b00. The B/W pass
 * draws every non-white level as ink, which is what makes 0b00 come out black for level 3.
 * Any other code for black would lift it to grey.
 *
 * Index order is the dither level order, which is NOT brightness order:
 * 0 white, 1 DARK grey, 2 LIGHT grey, 3 black.
 */

#include <cstdint>

namespace {
constexpr uint8_t kOverlayKeep = 0b00;
constexpr uint8_t kOverlayDark = 0b01;
constexpr uint8_t kOverlayLight = 0b10;
}  // namespace

const uint8_t* grayscaleCodeTable() {
  static constexpr uint8_t kTable[4] = {
      kOverlayKeep,
      kOverlayDark,
      kOverlayLight,
      kOverlayKeep,
  };
  return kTable;
}

// The quality (GRAY2) planes carry this value's bits directly, so it is the same overlay
// code as the medium table.
uint8_t mapQualityGray2Level(const uint8_t level) { return grayscaleCodeTable()[level & 3u]; }

const uint8_t* mediumTextCodeTable() { return grayscaleCodeTable(); }

int applyDeviceToneCurve(const int gray) { return gray; }
