#pragma once

#include <Arduino.h>
#include <InputManager.h>

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
  // FrontlightManager shares Picco's I2C bus and reinitializes Wire during
  // setup. Re-run the touch bring-up afterwards so the TMA525C owns the bus
  // with its required timeout and report state.
  void reinitializeTouch();
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

  enum class TouchSwipe : uint8_t { None, Up, Down, Left, Right };
  TouchSwipe touchSwipe() const { return touchSwipeDirection; }
  bool touchSwipeStart(float& nx, float& ny) const;

  enum class MotionGesture : uint8_t { None, Previous, Next };
  MotionGesture readMotionGesture(uint8_t orientation, uint8_t mode, uint8_t sensitivity);
  void startDeepSleep();
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
  uint8_t touchPressedEvents = 0;
  uint8_t touchReleasedEvents = 0;
  TouchSwipe touchSwipeDirection = TouchSwipe::None;
  float touchSwipeStartNx = 0.0f;
  float touchSwipeStartNy = 0.0f;
  bool lastTouchPressed = false;
  bool touchGestureActive = false;
  InputManager::TouchPoint touchGestureStart = {false, 0, 0, 0};
  InputManager::TouchPoint touchGestureLast = {false, 0, 0, 0};
  mutable int batteryCachedPercent = 0;
  mutable unsigned long batteryLastPollMs = 0;
  static constexpr unsigned long BATTERY_POLL_MS = 1500;
  bool motionSensorInitialized = false;
  bool motionGestureInProgress = false;
  unsigned long motionLastPollMs = 0;
  unsigned long motionSensorStartedMs = 0;
  unsigned long motionLastGestureMs = 0;
  void serviceTouchGestures();
};

extern HalGPIO gpio;
