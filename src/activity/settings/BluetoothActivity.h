#pragma once

#include <cstdint>
#include <functional>
#include <utility>

#include "activity/Activity.h"

/** BLE HID pairing screen for page-turner remotes. */
class BluetoothActivity final : public Activity {
 public:
  BluetoothActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::function<void()> onBack)
      : Activity("Bluetooth", renderer, mappedInput), onBack_(std::move(onBack)) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;

 private:
  void render();
  void startScan();
  bool handleTouchInput();
  void handleMappingInput();
  void openDevices();
  void openDevice(const char* addr, const char* name);
  void openPairedDevice(int index);
  void openDiscoveredDevice(int index);
  void startLearning(int actionIndex);
  void finishLearning();
  bool detailIsConnected() const;
  const char* detailTitle() const;
  uint32_t signature() const;

  enum class View : uint8_t { Menu, Devices, Device, Scan, Mapping };

  View view_ = View::Menu;
  int learningAction_ = -1;
  uint32_t learningStartedAt_ = 0;
  uint32_t renderedSignature_ = 0;
  char detailAddr_[18] = {};
  char detailName_[32] = {};
  std::function<void()> onBack_;
};
