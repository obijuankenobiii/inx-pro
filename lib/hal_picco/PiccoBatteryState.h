#pragma once

#include "PiccoBatteryEstimate.h"

class PiccoBatteryState {
 public:
  void update(const BatteryMonitor::Status& sample, uint32_t elapsedMs) {
    estimate_.update(sample, elapsedMs);
    if (sample.chargingKnown) charging_ = sample.charging;
    if (sample.externalPowerKnown) externalPower_ = sample.externalPower;
  }

  uint16_t percentage() const { return estimate_.percentage(); }
  bool isCharging() const { return charging_; }
  bool isUsbConnected(bool nativeUsbConnected = false) const { return nativeUsbConnected || externalPower_; }

 private:
  PiccoBatteryEstimate estimate_;
  bool charging_ = false;
  bool externalPower_ = false;
};
