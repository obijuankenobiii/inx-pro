#include "PiccoBatteryState.h"

#include <cassert>
#include <cstdio>

static BatteryMonitor::Status sample(uint16_t percent, uint16_t mv, bool usb, bool charging, bool full = false) {
  BatteryMonitor::Status s;
  s.supported = true;
  s.percentageKnown = s.millivoltsKnown = s.chargingKnown = s.externalPowerKnown = s.chargeCompleteKnown = true;
  s.percentage = percent;
  s.millivolts = mv;
  s.externalPower = usb;
  s.charging = charging;
  s.chargeComplete = full;
  return s;
}

int main() {
  PiccoBatteryState state;
  state.update(sample(80, 4187, true, true), 0);
  assert(state.percentage() == 80 && state.isCharging() && state.isUsbConnected());


  state.update({}, 1500);
  assert(state.percentage() == 80 && state.isCharging() && state.isUsbConnected());
  auto unreadCharger = sample(50, 3812, false, false);
  unreadCharger.chargingKnown = unreadCharger.externalPowerKnown = unreadCharger.chargeCompleteKnown = false;
  state.update(unreadCharger, 1500);
  assert(state.percentage() == 80 && state.isCharging() && state.isUsbConnected());


  state.update(sample(98, 4280, true, false, true), 1500);
  assert(state.percentage() == 100 && !state.isCharging() && state.isUsbConnected());


  state.update(sample(80, 4065, false, false), 1500);
  assert(state.percentage() == 100 && !state.isCharging() && !state.isUsbConnected());
  for (int i = 0; i < 80; ++i) state.update(sample(80, 4065, false, false), 1500);
  assert(state.percentage() == 100);
  for (int i = 0; i < 2400; ++i) state.update(sample(80, 4065, false, false), 1500);
  assert(state.percentage() == 80);


  state.update(sample(5, 3565, false, false), 1500);
  assert(state.percentage() == 5);


  PiccoBatteryState sleeping;
  sleeping.update(sample(80, 4065, false, false), 0);
  sleeping.update(sample(30, 3730, false, false), 8 * 60 * 60 * 1000);
  assert(sleeping.percentage() == 30);


  PiccoBatteryState unknown;
  unknown.update(unreadCharger, 0);
  assert(!unknown.isUsbConnected() && !unknown.isCharging());


  assert(unknown.isUsbConnected(true) && !unknown.isCharging());
  assert(!unknown.isUsbConnected(false));
  assert(unknown.percentage() == 50);


  auto noVoltage = sample(0, 0, true, true);
  noVoltage.percentageKnown = noVoltage.millivoltsKnown = false;
  unknown.update(noVoltage, 1500);
  assert(unknown.isUsbConnected() && unknown.isCharging());
  assert(unknown.percentage() == 50);
  std::puts("Picco battery state checks passed");
}
