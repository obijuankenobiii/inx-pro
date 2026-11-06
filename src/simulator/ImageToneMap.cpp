#include <cstdint>

const uint8_t *grayscaleCodeTable() {
  static constexpr uint8_t table[4] = {0b00, 0b11, 0b10, 0b01};
  return table;
}

uint8_t mapQualityGray2Level(const uint8_t level) {
  const uint8_t l = level & 3u;
  return l == 1u ? 2u : (l == 2u ? 1u : l);
}

const uint8_t *mediumTextCodeTable() { return grayscaleCodeTable(); }
int applyDeviceToneCurve(const int gray) { return gray; }
