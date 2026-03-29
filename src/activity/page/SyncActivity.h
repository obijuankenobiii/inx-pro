#pragma once

/**
 * @file SyncActivity.h
 * @brief Public interface and types for SyncActivity.
 */

#include <functional>
#include <memory>

#include "Page.h"

enum class NetworkMode { JOIN_NETWORK, CONNECT_CALIBRE, CREATE_HOTSPOT, OPDS_BROWSER };

class SyncActivity final : public Page {
 public:
  SyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
               const std::function<void(NetworkMode)>& onModeSelected)
      : Page("Device Management", renderer, mappedInput), onModeSelected(onModeSelected) {}

  const char* name() const override { return "Device Management"; }

  void onEnter() override;
  void onExit() override;
  void loop() override;

 private:
  int selectedIndex = 0;
  bool selectedVisible = false;
  std::unique_ptr<Activity> subActivity;

  const std::function<void(NetworkMode)> onModeSelected;

  void content() override;
  void enter(Activity* activity);
  void exit();
};
