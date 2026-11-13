#pragma once

#include <BatteryMonitor.h>

#include <algorithm>
#include <cstdint>

class PiccoBatteryEstimate {
 public:
  enum class Power : uint8_t { Unknown, Battery, Charging, External, Full };
  struct State {
    int32_t percentQ8;
    uint32_t settlingMs;
    Power power;
  };

  static constexpr uint32_t SETTLING_MS = 120000;
  static constexpr uint32_t FILTER_MS = 180000;
  static constexpr uint32_t REBASE_MS = 1800000;

  bool known() const { return state_.percentQ8 >= 0; }
  uint16_t percentage() const { return known() ? static_cast<uint16_t>((state_.percentQ8 + 128) / 256) : 0; }

  uint16_t update(const BatteryMonitor::Status& sample, uint32_t elapsedMs) {
    if (!sample.percentageKnown || !sample.millivoltsKnown || sample.percentage > 100 || sample.millivolts < 2500 ||
        sample.millivolts > 4500)
      return percentage();

    Power power = Power::Unknown;
    if (sample.externalPowerKnown && sample.chargingKnown) {
      if (!sample.externalPower)
        power = Power::Battery;
      else if (sample.chargeCompleteKnown && sample.chargeComplete && sample.millivolts >= 4229)
        power = Power::Full;
      else
        power = sample.charging ? Power::Charging : Power::External;
    }
    if (sample.percentage >= 100) {
      power = Power::Full;
    }

    const uint16_t voltagePercent =
        power == Power::Unknown ? std::min<uint16_t>(sample.percentage, 99) : sample.percentage;
    const int32_t target = (power == Power::Full ? 100 : voltagePercent) * 256;
    const bool rebase = !known() || elapsedMs >= REBASE_MS;
    const bool low = sample.millivolts <= 3636 && sample.percentage <= 10;
    if (known() && power == Power::Unknown && !low) return percentage();
    if (rebase || power == Power::Full || (low && target < state_.percentQ8)) {
      state_ = {target, 0, power};
      return percentage();
    }
    if (power != state_.power) {
      state_.power = power;
      state_.settlingMs = SETTLING_MS;
      return percentage();
    }
    if (state_.settlingMs > 0) {
      const uint32_t heldMs = std::min(elapsedMs, state_.settlingMs);
      state_.settlingMs -= heldMs;
      elapsedMs -= heldMs;
    }
    const int32_t delta = target - state_.percentQ8;
    const uint32_t denominator = FILTER_MS + elapsedMs;
    const int32_t step = static_cast<int32_t>(
        (static_cast<uint64_t>(delta < 0 ? -delta : delta) * elapsedMs + denominator / 2) / denominator);
    state_.percentQ8 += delta < 0 ? -step : step;
    return percentage();
  }

 private:
  State state_{-1, 0, Power::Unknown};
};
