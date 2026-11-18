#pragma once

/**
 * @file SyncActivity.h
 * @brief Public interface and types for SyncActivity.
 */

#include <functional>
#include <memory>

#include "Page.h"

enum class NetworkMode { JOIN_NETWORK, CONNECT_CALIBRE, CREATE_HOTSPOT };

class SyncActivity final : public UiPage {
 public:
  SyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
               const std::function<void(NetworkMode)>& onModeSelected)
      : UiPage("Device", renderer, mappedInput), onModeSelected(onModeSelected) {}

  const char* name() const override { return showFileTransfer_ ? "File Transfer" : "Device"; }

  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool back() override;

 private:
  std::unique_ptr<Activity> subActivity;

  const std::function<void(NetworkMode)> onModeSelected;
  bool showFileTransfer_ = false;
  int selectedIndex_ = 0;

  void content() override;
  void activateItem(int index);
  void enter(Activity* activity);
  void exit();
};
