#pragma once

/**
 * @file DeviceInfoActivity.h
 * @brief Public interface and types for DeviceInfoActivity.
 */

#include <functional>
#include <string>
#include <vector>

#include "activity/Activity.h"

/**
 * Static device info screen reached from Device Management -> Device: firmware version, chip/board
 * hardware, and ESP-IDF version, replacing the old device-identity image viewer. All values are read once
 * on entry (cheap synchronous ESP32 Arduino core calls) - no background task is needed, unlike this
 * screen's siblings (KOReaderSettingsActivity, OtaUpdateActivity) which do real I/O.
 */
class DeviceInfoActivity final : public Activity {
 public:
  explicit DeviceInfoActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                              const std::function<void()>& onClose)
      : Activity("DeviceInfo", renderer, mappedInput), onClose(onClose) {}

  void onEnter() override;
  void loop() override;

 private:
  struct InfoRow {
    std::string label;
    std::string value;
  };

  const std::function<void()> onClose;
  std::vector<InfoRow> rows;
  bool updateRequired = false;

  void buildRows();
  void render();
};
