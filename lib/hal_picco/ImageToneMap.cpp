#include <cstdint>

// Picco medium rendering is SSD2677 overlay AA, not the controller's absolute
// four-level image encoding.  The overlay LUT uses the B/W base to distinguish
// white and black (both use mask 00), then applies 11 for dark gray and 01 for
// light gray.  Using the absolute-image order here makes light gray select the
// black bucket and makes black select a gray bucket.
const uint8_t* grayscaleCodeTable() {
  static constexpr uint8_t kOverlayTable[4] = {
      0b00,  // white: leave the B/W base unchanged
      0b11,  // dark gray
      0b01,  // light gray
      0b00,  // black: leave the B/W base unchanged
  };
  return kOverlayTable;
}

uint8_t mapQualityGray2Level(const uint8_t level) {
  const uint8_t l = level & 3u;
  // SSD2677 absolute image encoding is 00=black, 01=dark gray,
  // 10=light gray, 11=white.  The renderer's level order is the reverse:
  // 0=white, 1=dark gray, 2=light gray, 3=black.
  static constexpr uint8_t kAbsoluteImageTable[4] = {
      0b11,  // white
      0b01,  // dark gray
      0b10,  // light gray
      0b00,  // black
  };
  return kAbsoluteImageTable[l];
}

const uint8_t* mediumTextCodeTable() { return grayscaleCodeTable(); }
int applyDeviceToneCurve(const int gray) { return gray; }
