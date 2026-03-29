#pragma once

/**
 * @file HalGPIO.h
 * @brief Hardware abstraction for the Xteink X4 Pro.
 *
 * Same public interface as lib/hal_sticky/HalGPIO.h so src/ compiles unchanged.
 * Delegates to freeink-sdk (git submodule) — InputManager (GPIO0/7/3 buttons +
 * GT911 touch incl. the capacitive Home key), BatteryMonitor (CW2017 gauge),
 * Rtc (BM8563, PCF8563-compatible).
 *
 * Peripherals the X4 Pro does NOT have, and which are therefore stubbed rather
 * than wired: IMU (no tilt-to-turn), SHT40 environment sensor, PDM microphone
 * (voice notes compile out via FREEINK_CAP_MIC), buzzer.
 *
 * It also does NOT carry Sticky's GT911 geometry overrides. Those are calibration
 * for that unit's digitizer mounting; the X4 Pro's swapXY/flipY are corner-tap
 * confirmed upstream in BoardConfig::XTEINK_X4_PRO and must be left alone.
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
    // UI convention: 1=Monday .. 7=Sunday. The FreeInk RTC uses 0=Sunday.
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

  /** Drops a swipe latched for this update cycle, plus any button edges synthesized from
   *  it. A swipe stays set for the whole cycle so it survives the touch release; without
   *  this, a gesture that closes an activity is still latched when the next activity's
   *  loop runs and gets handled a second time there. */
  void discardTouchSwipe();

  /** Swallow the touch that is currently held, up to and including its release.
   *  A finger can stay down across an activity transition — a page-turn refresh here runs
   *  ~1.8 s — and the release then synthesizes a tap/swipe on whichever screen is showing
   *  by the time it lands. Call this when an activity exits with a touch still in progress
   *  so that gesture belongs to the activity that started it. */
  void ignoreCurrentTouchUntilRelease();




  // Raw touch passthrough, for activities that want to hit-test a tap against
  // their own on-screen layout (e.g. "open the book cover that was tapped")
  // rather than the touch-to-button gesture synthesis in update(). nx/ny are
  // normalized 0..1 in the panel's native (unrotated) frame — callers are
  // responsible for mapping to their own screen orientation if it differs.
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
  // No IMU on this board — always returns MotionGesture::None.
  MotionGesture readMotionGesture(uint8_t orientation, uint8_t mode, uint8_t sensitivity);

  void startDeepSleep();

  int getBatteryPercentage() const;

  bool isUsbConnected() const;

  bool readDateTime(DateTime& outDateTime) const;
  bool writeDateTime(const DateTime& dateTime) const;
  bool syncRtcFromSystemTime() const;

  enum class WakeupReason { PowerButton, AfterFlash, AfterUSBPower, Other };

  WakeupReason getWakeupReason() const;

  // No environment sensor on this board — always returns false and leaves the
  // outputs unchanged. Kept so shared src/ code compiles against one interface.
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

  // Touch-gesture-synthesized button edges for this update() cycle, layered on
  // top of inputMgr's real button edges (see update()).
  uint8_t touchPressedEvents = 0;
  uint8_t touchReleasedEvents = 0;
  TouchSwipe touchSwipeDirection = TouchSwipe::None;
  float touchSwipeStartNx = 0.0f;
  float touchSwipeStartNy = 0.0f;

  mutable int batteryCachedPercent = 0;
  mutable unsigned long batteryLastPollMs = 0;
  static constexpr unsigned long BATTERY_POLL_MS = 1500;

  // Motion-gesture debounce state.
  bool motionSensorInitialized = false;
  bool motionGestureInProgress = false;
  unsigned long motionLastPollMs = 0;
  unsigned long motionSensorStartedMs = 0;
  unsigned long motionLastGestureMs = 0;

  void serviceTouchGestures();

  // Set while a touch that began before an activity transition is being swallowed.
  bool suppressTouchUntilRelease = false;
};

extern HalGPIO gpio;


/**
 * Whether a tap should be discarded because it is really the tail of a bottom-edge swipe.
 *
 * @param ny      touch-down point in logical screen coords (0 = top, 1 = bottom)
 * @param heldMs  how long the touch was held before release
 *
 * Defined per device in lib/hal_<device>/HalGPIO.cpp; return false to disable.
 */
bool suppressBottomEdgeTap(float ny, unsigned long heldMs);
