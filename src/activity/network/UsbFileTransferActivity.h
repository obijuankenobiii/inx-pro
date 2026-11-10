#pragma once

/**
 * @file UsbFileTransferActivity.h
 * @brief Basic USB file-transfer screen.
 */

#include <functional>

#include <BoardConfig.h>

#include "activity/Activity.h"

#if FREEINK_CAP_USB_MSC
#include <UsbMassStorage.h>
#endif

class UsbFileTransferActivity final : public Activity {
 public:
  UsbFileTransferActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                          const std::function<void()>& onBack)
      : Activity("USB", renderer, mappedInput), onBack(onBack) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool preventAutoSleep() override;

 private:
  enum class State { Preparing, WaitingForHost, Connected, Accessed, Disconnecting, Error };

  const std::function<void()> onBack;
  State state = State::Preparing;

#if FREEINK_CAP_USB_MSC
  freeink::UsbMassStorage usb;
  bool storageDetached = false;
  bool usbSessionStarted = false;
  bool restartRequested = false;
#endif

  void render() const;
  void finishAndGoBack();
  void requestDisconnect();
};
