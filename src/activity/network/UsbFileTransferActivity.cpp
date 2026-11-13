/**
 * @file UsbFileTransferActivity.cpp
 * @brief Basic USB file-transfer screen.
 */

#include "UsbFileTransferActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>

#include <algorithm>

#include "activity/page/SubPage.h"
#include "activity/page/components/global/Button.h"
#include "activity/page/components/widget/BaseCarousel.h"
#include "platform/UsbSerialJtagHandoff.h"
#include "state/HomeTheme.h"
#include "state/SystemSetting.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "system/UiLayout.h"

namespace {
constexpr int kActionMargin = 20;
constexpr int kPanelGap = 15;
constexpr int kPanelButtonGap = 20;
constexpr int kCardPadding = 20;

#if FREEINK_DEVICE_X4PRO || FREEINK_DEVICE_PICCO || FREEINK_DEVICE_STICKY || FREEINK_DEVICE_MURPHY_M4
constexpr int kIllustrationWidth = 400;
#if FREEINK_DEVICE_X4PRO
constexpr int kIllustrationHeight = 200;
#else
constexpr int kIllustrationHeight = 212;
#endif
#endif

bool contains(const ButtonBounds& bounds, const int x, const int y) {
  return x >= bounds.x && x < bounds.x + bounds.width && y >= bounds.y && y < bounds.y + bounds.height;
}

ButtonBounds usbActionButtonBounds(const GfxRenderer& renderer, const char* label) {
  const int width = Button::width(renderer, label, systemFontId());
  return {renderer.getScreenWidth() - kActionMargin - width,
          renderer.getScreenHeight() - kActionMargin - Button::height, width, Button::height};
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
      const char* actionLabel = state == State::WaitingForHost ? "Cancel" : "Disconnect";
      const ButtonBounds disconnectButton = usbActionButtonBounds(renderer, actionLabel);
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

  const int contentTop = SubPage::header(renderer, "File Transfer", "Usb");
  const int screenHeight = renderer.getScreenHeight();
  const int centerY = contentTop + (screenHeight - contentTop) / 2;

  const char* message = "Preparing USB transfer...";
  const char* detail = "";
  switch (state) {
    case State::Preparing:
      break;
    case State::WaitingForHost:
      message = "Not connected";
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
  const int screenWidth = renderer.getScreenWidth();
  const int illustrationX = (screenWidth - kIllustrationWidth) / 2;
  const int illustrationY = contentTop;
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
  const int panelY = illustrationY + kIllustrationHeight + kPanelGap;
  const char* actionLabel = state == State::WaitingForHost ? "Cancel" : "Disconnect";
  const ButtonBounds disconnectButton = usbActionButtonBounds(renderer, actionLabel);
  const int panelBottom = showDisconnectButton
                              ? disconnectButton.y - kPanelButtonGap
                              : screenHeight - kActionMargin;
  const int panelHeight = std::max(1, panelBottom - panelY);
  for (int py = panelY; py < panelBottom; py += 2) {
    const int offset = ((py - panelY) & 2) != 0 ? 0 : 1;
    for (int px = offset; px < screenWidth; px += 2) renderer.drawPixel(px, py, true);
  }

  const int cardX = kCardPadding;
  const int cardY = panelY + kCardPadding;
  const int cardWidth = screenWidth - 2 * kCardPadding;
  const int font = systemFontId();
  const int lineHeight = renderer.text.getLineHeight(font);
  const int instructionFont = systemFontIdForPointSize(12);
  const int instructionLine = renderer.text.getLineHeight(instructionFont);
  const int tagFont = systemFontIdForPointSize(12);
  const int tagLine = renderer.text.getLineHeight(tagFont);
  const int tagHeight = tagLine + 8;
  int contentHeight = UiLayout::LIST_ITEM_HEIGHT;
  if (state == State::WaitingForHost) {
    contentHeight = 2 * kCardPadding + tagHeight + 10 + 3 * instructionLine + 8;
  } else if (state == State::Connected || state == State::Accessed) {
    contentHeight = 2 * kCardPadding + std::max(lineHeight, tagHeight);
  }
  const int cardHeight = std::max(1, std::min(contentHeight, panelHeight - 2 * kCardPadding));
  const bool rounded = SETTINGS.bitmapRoundedCorners != 0;
  BaseCarousel::renderShadow(renderer, cardX + 4, cardY + 4, cardWidth, cardHeight,
                             HomeTheme::CarouselShadowStyle::Gray);
  renderer.rectangle.fill(cardX, cardY, cardWidth, cardHeight, false, rounded);
  renderer.rectangle.render(cardX, cardY, cardWidth, cardHeight, true, rounded);

  if (state == State::WaitingForHost) {
    const int tagWidth = renderer.text.getWidth(tagFont, "Not connected", EpdFontFamily::BOLD) + 24;
    const int tagX = cardX + cardWidth - kCardPadding - tagWidth;
    const int tagY = cardY + kCardPadding;
    renderer.rectangle.fill(tagX, tagY, tagWidth, tagLine + 8, true);
    renderer.text.render(tagFont, tagX + 12, tagY + 4, "Not connected", false, EpdFontFamily::BOLD);

    const int instructionY = tagY + tagLine + 10;
    renderer.text.render(instructionFont, cardX + kCardPadding, instructionY, "Instruction", true,
                         EpdFontFamily::BOLD);
    renderer.text.render(instructionFont, cardX + kCardPadding, instructionY + instructionLine + 4,
                         "1. Connect cable to device.", true, EpdFontFamily::REGULAR);
    renderer.text.render(instructionFont, cardX + kCardPadding,
                         instructionY + 2 * instructionLine + 8,
                         "2. Connect the device to your PC.", true, EpdFontFamily::REGULAR);
  } else if (state == State::Connected || state == State::Accessed) {
    const int connectedTagWidth = renderer.text.getWidth(tagFont, "Connected", EpdFontFamily::BOLD) + 24;
    const int connectedTagX = cardX + cardWidth - kCardPadding - connectedTagWidth;
    const int connectedTagY = cardY + kCardPadding;
    renderer.rectangle.fill(connectedTagX, connectedTagY, connectedTagWidth, tagHeight, true);
    renderer.text.render(tagFont, connectedTagX + 12,
                         connectedTagY + (tagHeight - tagLine) / 2,
                         "Connected", false, EpdFontFamily::BOLD);

    const int progressY = connectedTagY + (tagHeight - lineHeight) / 2;
    renderer.text.render(font, cardX + kCardPadding, progressY, "Transfer data", true,
                         EpdFontFamily::REGULAR);
  } else {
    const int messageY = detail[0] == '\0'
                             ? cardY + (cardHeight - lineHeight) / 2
                             : cardY + (cardHeight - 2 * lineHeight - 4) / 2;
    renderer.text.centered(font, messageY, status, true);
    if (detail[0] != '\0') renderer.text.centered(font, messageY + lineHeight + 4, detail, true);
  }
  if (showDisconnectButton) {
    Button::render(renderer, disconnectButton, actionLabel, true, font);
  }
#else
  renderer.text.centered(systemFontId(), centerY - 24, message, true);
  if (detail[0] != '\0') renderer.text.centered(systemFontId(), centerY + 10, detail, true);
  renderer.text.centered(systemFontIdForPointSize(8), screenHeight - 30,
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
