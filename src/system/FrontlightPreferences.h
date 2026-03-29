#pragma once

#include <cstdint>

namespace frontlight_preferences {

struct Settings {
  uint8_t enabled = 0;
  uint8_t brightness = 50;
  uint8_t warmPercent = 50;
};

bool load(Settings& settings);
bool save(const Settings& settings);

}  // namespace frontlight_preferences
