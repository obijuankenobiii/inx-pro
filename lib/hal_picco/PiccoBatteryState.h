#pragma once

#include <BatteryEstimate.h>

// Keep the UI's power fields from the same SDK sample. Unknown telemetry must
// not turn a previously observed connected cable into a disconnected one.
class PiccoBatteryState {
 public:
  void update(const BatteryMonitor::Status& sample, uint32_t elapsedMs) {
    estimate_.update(sample, elapsedMs);
    if (sample.chargingKnown) charging_ = sample.charging;
    if (sample.externalPowerKnown) externalPower_ = sample.externalPower;
  }

  uint16_t percentage() const { return estimate_.percentage(); }
  bool isCharging() const { return charging_; }
  // A live native-USB connection independently proves input power. It does
  // not prove active battery charging; keep isCharging() tied to the charger.
  bool isUsbConnected(bool nativeUsbConnected = false) const { return nativeUsbConnected || externalPower_; }

 private:
  BatteryEstimate estimate_;
  bool charging_ = false;
  bool externalPower_ = false;
};
