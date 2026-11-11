#pragma once

#include <Arduino.h>
#include <InputManager.h>
#include "PiccoBatteryState.h"
#include "PiccoCharger.h"
#include "system/TouchGestureClassifier.h"

/** Application GPIO/input interface for the BOOX/Onyx Picco. */
class HalGPIO {
 public:
  struct DateTime {
    uint16_t year = 0;
    uint8_t month = 0;
    uint8_t day = 0;
    uint8_t hour = 0;
    uint8_t minute = 0;
    uint8_t second = 0;
    uint8_t weekday = 0;
  };

  void begin();
  void updateDuringDisplay();
  // FrontlightManager shares the existing Wire bus and changes its timeout.
  // Restore the touch timeout without deleting the bus or resetting touch.
  void restoreTouchBusSettings();
  void update();
  void injectOneShotPress(uint8_t buttonIndex);
  bool isPressed(uint8_t buttonIndex) const;
  bool wasPressed(uint8_t buttonIndex) const;
  bool wasAnyPressed() const;
  bool wasReleased(uint8_t buttonIndex) const;
  bool wasAnyReleased() const;
  unsigned long getHeldTime() const;
  bool hasToggleSwitch() const;
  bool isToggleSwitchOn() const;
  bool wasToggleSwitchChanged() const;
  bool hasTouch() const;
  bool isTouchPressed() const;
  bool wasTouchPressedAt(float& nx, float& ny) const;
  bool isTouchHeldAt(float& nx, float& ny) const;
  bool wasTouchActivity() const;
  bool wasTouchTap(float& nx, float& ny) const;
  unsigned long lastTouchHeldMs() const;

  /** Drops a swipe latched for the current input cycle. */
  void discardTouchSwipe();

  /** Swallows the currently held touch through its release. */
  void ignoreCurrentTouchUntilRelease();

  enum class TouchSwipe : uint8_t { None, Up, Down, Left, Right };
  TouchSwipe touchSwipe() const { return touchSwipeDirection; }
  bool touchSwipeStart(float& nx, float& ny) const;
  bool touchSwipeEnd(float& nx, float& ny) const;

  enum class MotionGesture : uint8_t { None, Previous, Next };
  MotionGesture readMotionGesture(uint8_t orientation, uint8_t mode, uint8_t sensitivity);
  /** Enter deep sleep; in toggle-power mode also wake when the slide switch returns on. */
  void startDeepSleep(bool wakeOnToggle = false);
  int getBatteryPercentage() const;
  bool isCharging() const;
  bool isUsbConnected() const;
  bool readDateTime(DateTime& outDateTime) const;
  bool writeDateTime(const DateTime& dateTime) const;
  bool syncRtcFromSystemTime() const;
  enum class WakeupReason { PowerButton, AfterFlash, AfterUSBPower, Other };
  WakeupReason getWakeupReason() const;
  bool getTemperatureAndHumidity(float& outTempC, float& outHumidityPct) const;

  static constexpr uint8_t BTN_BACK = 0;
  static constexpr uint8_t BTN_CONFIRM = 1;
  static constexpr uint8_t BTN_LEFT = 2;
  static constexpr uint8_t BTN_RIGHT = 3;
  static constexpr uint8_t BTN_UP = 4;
  static constexpr uint8_t BTN_DOWN = 5;
  static constexpr uint8_t BTN_POWER = 6;

 private:
  InputManager inputMgr;
  TouchGestureClassifier touchGesture;
  unsigned long nextTouchRetryMs = 0;
  uint8_t touchPressedEvents = 0;
  uint8_t touchReleasedEvents = 0;
  TouchSwipe touchSwipeDirection = TouchSwipe::None;
  float touchSwipeStartNx = 0.0f;
  float touchSwipeStartNy = 0.0f;
  float touchSwipeEndNx = 0.0f;
  float touchSwipeEndNy = 0.0f;
  bool lastTouchPressed = false;
  bool suppressTouchUntilRelease = false;
  bool touchSwipeLatchedDuringDisplay = false;
  mutable PiccoBatteryState batteryState;
  mutable picco_charger::PollSchedule batteryPoll;
  mutable unsigned long batteryLastLogMs = 0;
  mutable int16_t batteryLastLoggedCharger = -2;
  mutable bool chargerProbeLogged = false;
  bool motionSensorInitialized = false;
  bool motionGestureInProgress = false;
  unsigned long motionLastPollMs = 0;
  unsigned long motionSensorStartedMs = 0;
  unsigned long motionLastGestureMs = 0;
  void serviceTouchGestures();
  void pollBattery() const;
};

extern HalGPIO gpio;
