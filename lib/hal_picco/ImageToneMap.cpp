#include <cstdint>






const uint8_t* grayscaleCodeTable() {
  static constexpr uint8_t kOverlayTable[4] = {
      0b00,
      0b11,
      0b01,
      0b00,
  };
  return kOverlayTable;
}

uint8_t mapQualityGray2Level(const uint8_t level) {
  const uint8_t l = level & 3u;



  static constexpr uint8_t kAbsoluteImageTable[4] = {
      0b11,
      0b01,
      0b10,
      0b00,
  };
  return kAbsoluteImageTable[l];
}

const uint8_t* mediumTextCodeTable() { return grayscaleCodeTable(); }
int applyDeviceToneCurve(const int gray) { return gray; }
