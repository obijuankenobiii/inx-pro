#include "PiccoCharger.h"
#include "PiccoBatteryState.h"

#include <initializer_list>
#include <Arduino.h>
#include <BoardConfig.h>
#include <Wire.h>

#include <array>
#include <cassert>
#include <cstdio>

namespace {
std::array<std::array<uint8_t, 16>, 8> registers{};
std::array<bool, 8> present{};
bool failReads = false;
unsigned registerWrites = 0;

void charger(uint8_t address, uint8_t identity = 0) {
  present[address] = true;
  registers[address][0x0b] = identity;
  registers[address][0x0a] = address << 5;
  registers[address][0x08] = 0x12;
}
}

bool hostI2cWrite(uint8_t address, const std::vector<uint8_t>& bytes) {
  if (address >= present.size() || !present[address]) return false;
  if (bytes.size() > 1) ++registerWrites;
  return true;
}

bool hostI2cRead(uint8_t address, uint8_t reg, uint8_t* out, size_t length) {
  if (failReads || address >= present.size() || !present[address] || reg >= 16 || length != 1) return false;
  *out = registers[address][reg];
  return true;
}

int main() {
  picco_charger::PollSchedule schedule;
  unsigned failedReads = 0;
  for (uint32_t now = 0; now < 120000; now += 10) {
    if (schedule.due(now)) {
      ++failedReads;
      schedule.complete(now, false);
    }
  }
  assert(failedReads == 4);  // a two-minute idle screen cannot flood at 1.5 s
  assert(schedule.due(120000) && schedule.elapsed(120000) == 30000);
  schedule.complete(120000, true);
  assert(!schedule.due(121499) && schedule.due(121500));
  schedule.complete(UINT32_MAX - 500, false);
  assert(!schedule.due(1000) && schedule.due(29499));

  assert(picco_charger::findAddress(Wire, 3) == 0);
  charger(5);
  assert(picco_charger::findAddress(Wire, 3) == 5);
  charger(3);
  assert(picco_charger::findAddress(Wire, 3) == 3);  // prefer the configured chip
  registers[3][0x0b] = 0xff;
  assert(picco_charger::findAddress(Wire, 3) == 5);  // wrong chip, despite ACK
  registers[5][0x0a] = 0;
  assert(picco_charger::findAddress(Wire, 3) == 0);  // zeros cannot identify a charger
  charger(4, 4);
  assert(picco_charger::findAddress(Wire, 3) == 4);
  failReads = true;
  assert(picco_charger::findAddress(Wire, 3) == 0);
  failReads = false;
  assert(registerWrites == 0);  // detection never changes a peripheral's registers

  present = {};
  charger(5);
  hostAdcMv = 2160;
  BatteryMonitor battery;
  assert(!battery.readStatus().externalPowerKnown);  // SDK's profile assumes 0x03
  const uint8_t detected = picco_charger::findAddress(Wire, BoardConfig::ACTIVE.batteryGauge.chargerAddr);
  assert(detected == 5);
  BoardConfig::ACTIVE.batteryGauge.chargerAddr = detected;
  auto status = battery.readStatus();
  assert(status.chargerStatus == 0x12 && status.chargingKnown && status.charging);
  assert(status.externalPowerKnown && status.externalPower);
  PiccoBatteryState state;
  state.update(status, 0);
  assert(state.isCharging() && state.isUsbConnected());
  registers[5][0x08] = 0x1a;
  status = battery.readStatus();
  state.update(status, 1500);
  assert(!state.isCharging() && state.isUsbConnected());
  registers[5][0x08] = 0;
  state.update(battery.readStatus(), 1500);
  assert(!state.isCharging() && !state.isUsbConnected());
  assert(registerWrites == 0);
  std::puts("Picco charger identification and SDK integration checks passed");
}
