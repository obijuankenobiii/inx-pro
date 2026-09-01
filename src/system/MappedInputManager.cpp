/**
 * @file MappedInputManager.cpp
 * @brief Definitions for MappedInputManager.
 */

#include "system/MappedInputManager.h"
#include "system/ScreenComponents.h"

#include <algorithm>
#include <HardwareSerial.h>
#include <utility>

#include <GfxRenderer.h>

#include "state/SystemSetting.h"
#include "system/TouchOrientation.h"

namespace {
using ButtonIndex = uint8_t;

struct FrontLayoutMap {
  ButtonIndex back;
  ButtonIndex confirm;
  ButtonIndex left;
  ButtonIndex right;
};

struct SideLayoutMap {
  ButtonIndex pageBack;
  ButtonIndex pageForward;
};

constexpr FrontLayoutMap kFrontLayouts[] = {
    {HalGPIO::BTN_BACK, HalGPIO::BTN_CONFIRM, HalGPIO::BTN_LEFT, HalGPIO::BTN_RIGHT},
    {HalGPIO::BTN_LEFT, HalGPIO::BTN_RIGHT, HalGPIO::BTN_BACK, HalGPIO::BTN_CONFIRM},
    {HalGPIO::BTN_CONFIRM, HalGPIO::BTN_LEFT, HalGPIO::BTN_BACK, HalGPIO::BTN_RIGHT},
    {HalGPIO::BTN_BACK, HalGPIO::BTN_CONFIRM, HalGPIO::BTN_RIGHT, HalGPIO::BTN_LEFT},
    {HalGPIO::BTN_RIGHT, HalGPIO::BTN_LEFT, HalGPIO::BTN_BACK, HalGPIO::BTN_CONFIRM},
};

constexpr SideLayoutMap kSideLayouts[] = {
    {HalGPIO::BTN_UP, HalGPIO::BTN_DOWN},
    {HalGPIO::BTN_DOWN, HalGPIO::BTN_UP},
};

MappedInputManager::Button remapDirectional180(const MappedInputManager::Button button) {
  switch (button) {
    case MappedInputManager::Button::Up:
      return MappedInputManager::Button::Down;
    case MappedInputManager::Button::Down:
      return MappedInputManager::Button::Up;
    case MappedInputManager::Button::Left:
      return MappedInputManager::Button::Right;
    case MappedInputManager::Button::Right:
      return MappedInputManager::Button::Left;
    case MappedInputManager::Button::PageBack:
      return MappedInputManager::Button::PageForward;
    case MappedInputManager::Button::PageForward:
      return MappedInputManager::Button::PageBack;
    default:
      return button;
  }
}
}

bool MappedInputManager::mapButton(const Button button, bool (HalGPIO::*fn)(uint8_t) const) const {
  const auto sideLayout = static_cast<SystemSetting::SIDE_BUTTON_LAYOUT>(SETTINGS.sideButtonLayout);
  const auto& front = kFrontLayouts[SystemSetting::BACK_CONFIRM_LEFT_RIGHT];
  const auto& side = kSideLayouts[sideLayout];

  const Button effective = invertDirectionalAxes180_ ? remapDirectional180(button) : button;

  switch (effective) {
    case Button::Back:
      return (gpio.*fn)(front.back);
    case Button::Confirm:
      return (gpio.*fn)(front.confirm);
    case Button::Left:
      return (gpio.*fn)(front.left);
    case Button::Right:
      return (gpio.*fn)(front.right);
    case Button::Up:
      return (gpio.*fn)(HalGPIO::BTN_UP);
    case Button::Down:
      return (gpio.*fn)(HalGPIO::BTN_DOWN);
    case Button::Power:
#if FREEINK_DEVICE_STICKY
      return (gpio.*fn)(HalGPIO::BTN_POWER) || (gpio.*fn)(front.confirm);
#else
      return (gpio.*fn)(HalGPIO::BTN_POWER);
#endif
    case Button::PageBack:
      return (gpio.*fn)(side.pageBack);
    case Button::PageForward:
      return (gpio.*fn)(side.pageForward);
  }

  return false;
}

bool MappedInputManager::wasPressed(const Button button) const {
  if (injectedPressedButton_ == static_cast<int8_t>(button)) {
    injectedPressedButton_ = -1;
    if (button == Button::Power) INX_SERIAL.println("[STICKY][INPUT] power pressed injected");
    return true;
  }
  if (mapButton(button, &HalGPIO::wasPressed)) {
    if (button == Button::Power) INX_SERIAL.println("[STICKY][INPUT] power pressed gpio");
    return true;
  }

  const auto swipe = swipeForDefaultOrientation();
  if (swipe != HalGPIO::TouchSwipe::Up && swipe != HalGPIO::TouchSwipe::Down) return false;
  const bool sideNavigation = SETTINGS.mainMenuNav == SystemSetting::MAIN_MENU_NAV_SIDE;
  const Button swipeButton = swipe == HalGPIO::TouchSwipe::Up
                                 ? (sideNavigation ? Button::Right : Button::Down)
                                 : (sideNavigation ? Button::Left : Button::Up);
  return button == swipeButton;
}

bool MappedInputManager::wasReleased(const Button button) const {
  if (injectedReleasedButton_ == static_cast<int8_t>(button)) {
    injectedReleasedButton_ = -1;
    if (button == Button::Power) INX_SERIAL.println("[STICKY][INPUT] power released injected");
    return true;
  }
  if (mapButton(button, &HalGPIO::wasReleased)) {
    if (button == Button::Power) INX_SERIAL.println("[STICKY][INPUT] power released gpio");
    return true;
  }

  const auto swipe = swipeForDefaultOrientation();
  if (swipe != HalGPIO::TouchSwipe::Up && swipe != HalGPIO::TouchSwipe::Down) return false;
  const bool sideNavigation = SETTINGS.mainMenuNav == SystemSetting::MAIN_MENU_NAV_SIDE;
  const Button swipeButton = swipe == HalGPIO::TouchSwipe::Up
                                 ? (sideNavigation ? Button::Right : Button::Down)
                                 : (sideNavigation ? Button::Left : Button::Up);
  return button == swipeButton;
}

HalGPIO::TouchSwipe MappedInputManager::swipeForDefaultOrientation() const {
  return inx::touch::toDefaultOrientation(gpio.touchSwipe());
}

bool MappedInputManager::isPressed(const Button button) const { return mapButton(button, &HalGPIO::isPressed); }

bool MappedInputManager::wasAnyPressed() const { return gpio.wasAnyPressed(); }

bool MappedInputManager::wasAnyReleased() const { return gpio.wasAnyReleased(); }

HalGPIO::TouchSwipe MappedInputManager::swipeForRenderer(const GfxRenderer& renderer) const {
  return inx::touch::forOrientation(renderer.getOrientation(), gpio.touchSwipe());
}

bool MappedInputManager::wasTouchSwipeUpForRenderer(const GfxRenderer& renderer) const {
  return swipeForRenderer(renderer) == HalGPIO::TouchSwipe::Up;
}

bool MappedInputManager::wasTouchSwipeDownForRenderer(const GfxRenderer& renderer) const {
  return swipeForRenderer(renderer) == HalGPIO::TouchSwipe::Down;
}

bool MappedInputManager::wasTouchSwipeLeftForRenderer(const GfxRenderer& renderer) const {
  return swipeForRenderer(renderer) == HalGPIO::TouchSwipe::Left;
}

bool MappedInputManager::wasTouchSwipeRightForRenderer(const GfxRenderer& renderer) const {
  return swipeForRenderer(renderer) == HalGPIO::TouchSwipe::Right;
}

bool MappedInputManager::wasTouchPressedInScreen(const GfxRenderer& renderer, float& nx, float& ny) const {
  float nativeNx = 0.0f;
  float nativeNy = 0.0f;
  if (!gpio.wasTouchPressedAt(nativeNx, nativeNy)) {
    return false;
  }
  mapNativeTouchToScreen(renderer, nativeNx, nativeNy, nx, ny);
  return true;
}

bool MappedInputManager::isTouchHeldInScreen(const GfxRenderer& renderer, float& nx, float& ny) const {
  float nativeNx = 0.0f;
  float nativeNy = 0.0f;
  if (!gpio.isTouchHeldAt(nativeNx, nativeNy)) {
    return false;
  }
  mapNativeTouchToScreen(renderer, nativeNx, nativeNy, nx, ny);
  return true;
}

bool MappedInputManager::wasTouchTapInScreen(const GfxRenderer& renderer, float& nx, float& ny) const {
  float nativeNx = 0.0f;
  float nativeNy = 0.0f;
  if (!readTouchTapNative(nativeNx, nativeNy)) {
    return false;
  }

  mapNativeTouchToScreen(renderer, nativeNx, nativeNy, nx, ny);

#if FREEINK_DEVICE_X4PRO
  {
    constexpr float kBottomBandNy = 0.97f;
    constexpr unsigned long kMinPhantomHoldMs = 600;
    const unsigned long heldMs = gpio.lastTouchHeldMs();
    if (ny >= kBottomBandNy && heldMs >= kMinPhantomHoldMs) {
      INX_SERIAL.printf("[TOUCH] tap ignored: bottom-edge swipe tail ny=%.3f held=%lums\n", ny, heldMs);
      return false;
    }
  }
#endif

  INX_SERIAL.printf("[TOUCH] MAP orientation=%d native=(%.3f,%.3f) logical=(%.3f,%.3f)\n",
                 static_cast<int>(renderer.getOrientation()), nativeNx, nativeNy, nx, ny);
  return true;
}

bool MappedInputManager::wasTouchSwipeUpInScreen(const GfxRenderer& renderer, float& nx, float& ny) const {
  if (swipeForRenderer(renderer) != HalGPIO::TouchSwipe::Up) return false;

  float nativeNx = 0.0f;
  float nativeNy = 0.0f;
  if (!gpio.touchSwipeStart(nativeNx, nativeNy)) return false;

  mapNativeTouchToScreen(renderer, nativeNx, nativeNy, nx, ny);
  return true;
}

bool MappedInputManager::wasTouchSwipeRightInScreen(const GfxRenderer& renderer, float& nx, float& ny) const {
  if (swipeForRenderer(renderer) != HalGPIO::TouchSwipe::Right) return false;

  float nativeNx = 0.0f;
  float nativeNy = 0.0f;
  if (!gpio.touchSwipeStart(nativeNx, nativeNy)) return false;

  mapNativeTouchToScreen(renderer, nativeNx, nativeNy, nx, ny);
  return true;
}

bool MappedInputManager::wasTouchSwipeLeftInScreen(const GfxRenderer& renderer, float& nx, float& ny) const {
  if (swipeForRenderer(renderer) != HalGPIO::TouchSwipe::Left) return false;

  float nativeNx = 0.0f;
  float nativeNy = 0.0f;
  if (!gpio.touchSwipeStart(nativeNx, nativeNy)) return false;

  mapNativeTouchToScreen(renderer, nativeNx, nativeNy, nx, ny);
  return true;
}

bool MappedInputManager::wasTouchSwipeDownInScreen(const GfxRenderer& renderer, float& nx, float& ny) const {
  if (swipeForRenderer(renderer) != HalGPIO::TouchSwipe::Down) return false;

  float nativeNx = 0.0f;
  float nativeNy = 0.0f;
  if (!gpio.touchSwipeStart(nativeNx, nativeNy)) return false;

  mapNativeTouchToScreen(renderer, nativeNx, nativeNy, nx, ny);
  return true;
}

bool MappedInputManager::readTouchTapNative(float& nx, float& ny) const {
  if (pendingTouchTap_) {
    pendingTouchTap_ = false;
    nx = pendingTouchNx_;
    ny = pendingTouchNy_;
    return true;
  }
  return wasTouchTap(nx, ny);
}

void MappedInputManager::mapNativeTouchToScreen(const GfxRenderer& renderer, const float nativeNx,
                                                const float nativeNy, float& nx, float& ny) const {
  inx::touch::nativeToScreen(renderer.getOrientation(), nativeNx, nativeNy, nx, ny);
  nx = std::max(0.0f, std::min(1.0f, nx));
  ny = std::max(0.0f, std::min(1.0f, ny));
}

void MappedInputManager::mapScreenTouchToNative(const GfxRenderer& renderer, const float screenNx,
                                                const float screenNy, float& nx, float& ny) const {
  inx::touch::screenToNative(renderer.getOrientation(), screenNx, screenNy, nx, ny);
  nx = std::max(0.0f, std::min(1.0f, nx));
  ny = std::max(0.0f, std::min(1.0f, ny));
}

void MappedInputManager::restoreTouchTapInScreen(const GfxRenderer& renderer, const float nx, const float ny) const {
  mapScreenTouchToNative(renderer, nx, ny, pendingTouchNx_, pendingTouchNy_);
  pendingTouchTap_ = true;
}

bool MappedInputManager::consumeHeaderBackTap(const GfxRenderer& renderer) const {
  float nativeNx = 0.0f;
  float nativeNy = 0.0f;
  if (!readTouchTapNative(nativeNx, nativeNy)) {
    return false;
  }

  float nx = 0.0f;
  float ny = 0.0f;
  mapNativeTouchToScreen(renderer, nativeNx, nativeNy, nx, ny);

  const int tapX = static_cast<int>(nx * renderer.getScreenWidth());
  const int tapY = static_cast<int>(ny * renderer.getScreenHeight());
  if (ScreenComponents::pageHeaderBackButtonHit(tapX, tapY)) {
    injectButtonEdge(Button::Back);
    return true;
  }

  pendingTouchTap_ = true;
  pendingTouchNx_ = nativeNx;
  pendingTouchNy_ = nativeNy;
  return false;
}

void MappedInputManager::injectButtonEdge(const Button button) const {
  injectedPressedButton_ = static_cast<int8_t>(button);
  injectedReleasedButton_ = static_cast<int8_t>(button);
}

MappedInputManager::MotionGesture MappedInputManager::readMotionGesture(const uint8_t orientation, const uint8_t mode,
                                                                        const uint8_t sensitivity) const {
#ifdef SIMULATOR
  return MotionGesture::None;
#else
  switch (gpio.readMotionGesture(orientation, mode, sensitivity)) {
    case HalGPIO::MotionGesture::Previous:
      return MotionGesture::Previous;
    case HalGPIO::MotionGesture::Next:
      return MotionGesture::Next;
    case HalGPIO::MotionGesture::None:
    default:
      return MotionGesture::None;
  }
#endif
}

unsigned long MappedInputManager::getHeldTime() const { return gpio.getHeldTime(); }

bool MappedInputManager::rawHalIsPressed(const uint8_t halButtonIndex) const { return gpio.isPressed(halButtonIndex); }

MappedInputManager::SideLabels MappedInputManager::mapSideLabels() const {
  const auto sideLayout = static_cast<SystemSetting::SIDE_BUTTON_LAYOUT>(SETTINGS.sideButtonLayout);

  static constexpr const char* kPrev = "\xC2\xAB";
  static constexpr const char* kNext = "\xC2\xBB";
  if (sideLayout == SystemSetting::NEXT_PREV) {
    return {kNext, kPrev};
  }
  return {kPrev, kNext};
}

MappedInputManager::Labels MappedInputManager::mapLabels(const char* back, const char* confirm, const char* previous,
                                                         const char* next) const {
  const char* p = previous;
  const char* n = next;
  if (invertDirectionalAxes180_) {
    std::swap(p, n);
  }

  return {back, confirm, p, n};
}

MappedInputManager::Labels MappedInputManager::mapLabelsWithReaderNav(const char* back, const char* confirm,
                                                                      const char* prevSym, const char* nextSym,
                                                                      bool landscapeDrawer) const {
  const char* p = prevSym;
  const char* n = nextSym;

  if (landscapeDrawer) {
    std::swap(p, n);
  } else {
    p = "Up";
    n = "Down";
  }

  return mapLabels(back, confirm, p, n);
}
