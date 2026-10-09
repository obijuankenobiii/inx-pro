#pragma once

#include <cstdint>

// The desktop simulator has no physical frontlight.  Keep the same public
// surface as the device manager so the real UI can be exercised without
// pulling the ESP32 BoardConfig into the native build.
class FrontlightManager {
 public:
  void begin() {}
  void setBrightness(uint8_t value) { brightness_ = value; }
  void setBrightnessLevel(uint8_t value) { brightness_ = value; }
  void off() { brightness_ = 0; }
  void on() { if (brightness_ == 0) brightness_ = 50; }
  void setColorTemperature(uint8_t value) { warmPercent_ = value; }

  bool present() const { return false; }
  bool hasColorTemperature() const { return false; }
  uint8_t brightness() const { return brightness_; }
  uint8_t brightnessLevel() const { return brightness_; }
  uint8_t colorTemperature() const { return warmPercent_; }

 private:
  uint8_t brightness_ = 0;
  uint8_t warmPercent_ = 50;
};
