#pragma once

/**
 * @file HalGPIO.h
 * @brief Hardware abstraction for Seeed reTerminal Sticky.
 * Delegates to freeink-sdk (git submodule) — InputManager (buttons + GT911
 * touch), BatteryMonitor (BQ27220), Rtc (PCF8563), Imu (LSM6DS3TR-C),
 * EnvironmentSensor (SHT40).
 *
 * The reader's voice-note activity uses a local legacy I2S-PDM recorder. The
 * vendored Buzzer and Microphone libraries remain unlinked because they target
 * a different platform API.
 */

#include <Arduino.h>
#include <InputManager.h>

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

  HalGPIO() = default;

  void begin();
  void update();
  void injectOneShotPress(uint8_t buttonIndex);
  bool isPressed(uint8_t buttonIndex) const;
  bool wasPressed(uint8_t buttonIndex) const;
  bool wasAnyPressed() const;
  bool wasReleased(uint8_t buttonIndex) const;
  bool wasAnyReleased() const;
  unsigned long getHeldTime() const;

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
