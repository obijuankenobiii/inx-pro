#pragma once

#include <cstdint>

namespace picco_charger {

// A missing charger must not trigger a failed register transaction on every
// header poll. Resume normal sampling as soon as a retry succeeds.
class PollSchedule {
 public:
  bool due(uint32_t now) const { return !sampled_ || now - lastPollMs_ >= intervalMs_; }
  bool sampled() const { return sampled_; }
  uint32_t elapsed(uint32_t now) const { return sampled_ ? now - lastPollMs_ : 0; }
  void complete(uint32_t now, bool available) {
    lastPollMs_ = now;
    intervalMs_ = available ? 1500 : 30000;
    sampled_ = true;
  }

 private:
  uint32_t lastPollMs_ = 0;
  uint32_t intervalMs_ = 1500;
  bool sampled_ = false;
};

template <typename Bus>
bool readRegister(Bus& bus, uint8_t address, uint8_t reg, uint8_t& value) {
  bus.beginTransmission(address);
  bus.write(reg);
  if (bus.endTransmission(false) != 0 || bus.requestFrom(address, uint8_t{1}, uint8_t{true}) != 1) return false;
  value = bus.read();
  return true;
}

// REG0B identifies SGM41562SA (0) or SGM41562 (4). REG0A[7:5]
// independently reports its factory-programmed address, from 1 through 7.
// An ACK alone is insufficient, especially on reserved low I2C addresses.
template <typename Bus>
bool matches(Bus& bus, uint8_t address) {
  if (address < 1 || address > 7) return false;
  bus.beginTransmission(address);
  if (bus.endTransmission() != 0) return false;
  uint8_t identity = 0xff, configuration = 0;
  return readRegister(bus, address, 0x0b, identity) && (identity == 0 || identity == 4) &&
         readRegister(bus, address, 0x0a, configuration) && (configuration >> 5) == address;
}

template <typename Bus>
uint8_t findAddress(Bus& bus, uint8_t preferred) {
  if (matches(bus, preferred)) return preferred;
  for (uint8_t address = 1; address <= 7; ++address) {
    if (address != preferred && matches(bus, address)) return address;
  }
  return 0;
}

}  // namespace picco_charger
