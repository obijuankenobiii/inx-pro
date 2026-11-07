/**
 * @file UsbFileTransferActivity.cpp
 * @brief Basic USB file-transfer screen.
 */

#include "UsbFileTransferActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>

#include "activity/page/SubPage.h"
#include "activity/page/components/global/Button.h"
#include "platform/UsbSerialJtagHandoff.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"

namespace {
constexpr int kDisconnectButtonWidth = 240;
constexpr int kDisconnectButtonHeight = 72;
constexpr int kDisconnectButtonGap = 28;

#if FREEINK_DEVICE_X4PRO || FREEINK_DEVICE_PICCO || FREEINK_DEVICE_STICKY || FREEINK_DEVICE_MURPHY_M4
constexpr int kIllustrationWidth = 400;
#if FREEINK_DEVICE_X4PRO
constexpr int kIllustrationHeight = 200;
#else
constexpr int kIllustrationHeight = 212;
#endif
constexpr int kIllustrationTextGap = 40;
#endif

bool contains(const ButtonBounds& bounds, const int x, const int y) {
  return x >= bounds.x && x < bounds.x + bounds.width && y >= bounds.y && y < bounds.y + bounds.height;
}

ButtonBounds disconnectButtonBounds(const GfxRenderer& renderer, const int statusY, const int textHeight) {
  return {(renderer.getScreenWidth() - kDisconnectButtonWidth) / 2,
          statusY + textHeight + kDisconnectButtonGap, kDisconnectButtonWidth, kDisconnectButtonHeight};
}
}  // namespace

#if FREEINK_DEVICE_X4PRO
#include "images/UsbTransferSetup.h"
#elif FREEINK_DEVICE_PICCO
#include "images/BooxUsbTransferSetup.h"
#elif FREEINK_DEVICE_STICKY || FREEINK_DEVICE_MURPHY_M4
#include "images/StickyMurphyUsbTransferSetup.h"
#endif

#if FREEINK_CAP_USB_MSC
#include <SDCardManager.h>
#endif

void UsbFileTransferActivity::onEnter() {
  Activity::onEnter();

  state = State::Preparing;
  render();

#if FREEINK_CAP_USB_MSC
  auto* const rawBlockDevice = SdMan.detachFilesystemForRawAccess();
  if (!rawBlockDevice || !usb.begin(rawBlockDevice)) {
    if (rawBlockDevice) SdMan.begin();
    state = State::Error;
    render();
    return;
  }

  storageDetached = true;
  usbSessionStarted = true;
  state = State::WaitingForHost;
#else
  state = State::Error;
#endif

  render();
}

void UsbFileTransferActivity::onExit() {
#if FREEINK_CAP_USB_MSC
  if (usb.active()) usb.end();
  if (storageDetached) {
    SdMan.begin();
    storageDetached = false;
  }
  if (usbSessionStarted && !restartRequested) {
    restartRequested = true;
    handoffUsbOtgToSerialJtag();
    delay(20);
    esp_restart();
    return;
  }
#endif
  Activity::onExit();
}

void UsbFileTransferActivity::loop() {
#if FREEINK_CAP_USB_MSC
  // Do not tear down the raw SD session while the host still has the drive
  // mounted. The host must eject it first; the eject/disconnect state below
  // then exits and performs the USB personality handoff safely.
  const bool hostOwnsStorage = state == State::Connected || state == State::Accessed;
  if (!hostOwnsStorage && SubPage::closeInput(renderer, mappedInput, onBack)) return;
#else
  if (SubPage::closeInput(renderer, mappedInput, onBack)) return;
#endif

#if FREEINK_CAP_USB_MSC
#if FREEINK_DEVICE_X4PRO || FREEINK_DEVICE_PICCO || FREEINK_DEVICE_STICKY || FREEINK_DEVICE_MURPHY_M4
  if (usbSessionStarted && state != State::Disconnecting && mappedInput.hasTouch()) {
    float tapX = 0.0f;
    float tapY = 0.0f;
    if (mappedInput.wasTouchTapInScreen(renderer, tapX, tapY)) {
      const int contentTop = navigation::Menu::topPadding + navigation::Menu::iconSize + 20;
      const int textHeight = renderer.text.getLineHeight(systemFontId());
      const int groupHeight = kIllustrationHeight + kIllustrationTextGap + textHeight;
      const int illustrationY = contentTop + (renderer.getScreenHeight() - contentTop - groupHeight) / 2;
      const int statusY = illustrationY + kIllustrationHeight + kIllustrationTextGap;
      const ButtonBounds disconnectButton = disconnectButtonBounds(renderer, statusY, textHeight);
      const int x = static_cast<int>(tapX * renderer.getScreenWidth());
      const int y = static_cast<int>(tapY * renderer.getScreenHeight());
      if (contains(disconnectButton, x, y)) {
        requestDisconnect();
        return;
      }
      mappedInput.restoreTouchTapInScreen(renderer, tapX, tapY);
    }
  }
#endif

  if (!usb.active()) return;

  const auto usbState = usb.state();
  State nextState = state;
  switch (usbState) {
    case freeink::UsbMassStorageState::WaitingForHost:
      nextState = State::WaitingForHost;
      break;
    case freeink::UsbMassStorageState::Connected:
      nextState = State::Connected;
      break;
    case freeink::UsbMassStorageState::Accessed:
      nextState = State::Accessed;
      break;
    case freeink::UsbMassStorageState::Ejected:
    case freeink::UsbMassStorageState::Disconnected:
      finishAndGoBack();
      return;
    case freeink::UsbMassStorageState::IoError:
      nextState = State::Error;
      break;
    case freeink::UsbMassStorageState::Idle:
      return;
  }

  if (nextState != state) {
    state = nextState;
    render();
  }
#endif
}

bool UsbFileTransferActivity::preventAutoSleep() {
#if FREEINK_CAP_USB_MSC
  return state == State::Connected || state == State::Accessed;
#else
  return false;
#endif
}

void UsbFileTransferActivity::render() const {
  renderer.clearScreen();

  const int contentTop = SubPage::header(renderer, "USB File Transfer");
  const int screenHeight = renderer.getScreenHeight();
  const int centerY = contentTop + (screenHeight - contentTop) / 2;

  const char* message = "Preparing USB transfer...";
  const char* detail = "";
  switch (state) {
    case State::Preparing:
      break;
    case State::WaitingForHost:
      message = "Waiting for connection";
      detail = "Connect a USB data cable";
      break;
    case State::Connected:
    case State::Accessed:
      message = "Device connected";
      detail = "Transfer files from your computer";
      break;
    case State::Disconnecting:
      message = "Disconnecting device";
      break;
    case State::Error:
      message = "Could not start USB transfer";
      detail = "Press Back to return";
      break;
  }

#if FREEINK_DEVICE_X4PRO || FREEINK_DEVICE_PICCO || FREEINK_DEVICE_STICKY || FREEINK_DEVICE_MURPHY_M4
#if FREEINK_CAP_USB_MSC
  const bool showDisconnectButton = usbSessionStarted && state != State::Disconnecting;
#else
  constexpr bool showDisconnectButton = false;
#endif
  const int textHeight = renderer.text.getLineHeight(systemFontId());
  // Keep the illustration/status group identical to the Preparing state. The
  // Disconnect button is an additional control below it and must not change
  // the vertical anchor of the artwork when USB startup advances states.
  const int groupHeight = kIllustrationHeight + kIllustrationTextGap + textHeight;
  const int illustrationX = (renderer.getScreenWidth() - kIllustrationWidth) / 2;
  const int illustrationY = contentTop + (screenHeight - contentTop - groupHeight) / 2;
#if FREEINK_DEVICE_X4PRO
  renderer.bitmap.icon(UsbTransferSetup, illustrationX, illustrationY, kIllustrationWidth, kIllustrationHeight);
#elif FREEINK_DEVICE_PICCO
  renderer.bitmap.icon(BooxUsbTransferSetup, illustrationX, illustrationY, kIllustrationWidth, kIllustrationHeight);
#else
  renderer.bitmap.icon(StickyMurphyUsbTransferSetup, illustrationX, illustrationY, kIllustrationWidth,
                       kIllustrationHeight);
#endif

  const char* status = message;
  if (state == State::Connected || state == State::Accessed) status = "USB transfer connected";
  const int statusY = illustrationY + kIllustrationHeight + kIllustrationTextGap;
  renderer.text.centered(systemFontId(), statusY, status, true);
  if (showDisconnectButton) {
    const ButtonBounds button = disconnectButtonBounds(renderer, statusY, textHeight);
    Button::render(renderer, button, "Disconnect", true, systemFontId());
  }
#else
  renderer.text.centered(systemFontId(), centerY - 24, message, true);
  if (detail[0] != '\0') renderer.text.centered(systemFontId(), centerY + 10, detail, true);
  renderer.text.centered(MONTSERRAT_8_FONT_ID, screenHeight - 30,
                         state == State::Connected || state == State::Accessed
                             ? "Eject the drive before unplugging"
                             : "Swipe up or press Back to cancel",
                         true);
#endif
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void UsbFileTransferActivity::requestDisconnect() {
#if FREEINK_CAP_USB_MSC
  if (!usbSessionStarted || state == State::Disconnecting) return;

  state = State::Disconnecting;
  render();
  // Give the user-visible state a moment to reach the panel before releasing
  // the USB personality and restarting into Serial/JTAG flashing mode.
  delay(100);
  usb.disconnectHost();
  finishAndGoBack();
#endif
}

void UsbFileTransferActivity::finishAndGoBack() {
  if (onBack) onBack();
}
