#include "HalGPIO.h"

#include <BatteryMonitor.h>
#include <BoardConfig.h>
#include <Imu.h>
#include <PowerManager.h>
#include <Rtc.h>
#include <driver/gpio.h>
#include <esp_sleep.h>
#include <esp_system.h>

#include "system/Logging.h"

#include <cmath>
#include <ctime>

namespace {
constexpr int kPiccoSwipeMinPx = 40;
constexpr unsigned long kPiccoSwipeMaxMs = 700;

uint8_t weekdayFromCalendarDate(const uint16_t year, const uint8_t month, const uint8_t day) {
  if (year < 1900 || month < 1 || month > 12 || day < 1 || day > 31) return 0;
  static constexpr uint8_t offsets[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  uint16_t adjustedYear = year;
  if (month < 3) --adjustedYear;
  const uint8_t sundayZero = static_cast<uint8_t>(
      (adjustedYear + adjustedYear / 4 - adjustedYear / 100 + adjustedYear / 400 + offsets[month - 1] + day) % 7);
  return static_cast<uint8_t>(sundayZero == 0 ? 7 : sundayZero);
}

freeink::Imu& imu() {
  static freeink::Imu value;
  return value;
}
}  // namespace

void HalGPIO::begin() {
  INX_SERIAL.begin(115200);
  INX_SERIAL.printf("[PICCO][BOOT] reset_reason=%d\n", static_cast<int>(esp_reset_reason()));
  BoardConfig::holdPowerRails();
  // Picco uses polled buttons. InputManager may report that the boot-time
  // power-button ISR service is absent while clearing its wake handler; do not
  // install the service here because ESP32-S3 performs that registration on
  // ipc0 and can overflow the tiny IPC task stack during early boot.
  inputMgr.begin();
  INX_SERIAL.printf("[PICCO] input ready touch=%d ranges=x:%u..%u y:%u..%u\n", inputMgr.hasTouch() ? 1 : 0,
                    BoardConfig::ACTIVE.touch.rawMinX, BoardConfig::ACTIVE.touch.rawMaxX,
                    BoardConfig::ACTIVE.touch.rawMinY, BoardConfig::ACTIVE.touch.rawMaxY);
}

void HalGPIO::reinitializeTouch() {
  inputMgr.begin();
  lastTouchPressed = false;
  touchGestureActive = false;
  INX_SERIAL.printf("[PICCO] touch reinitialized after frontlight: enabled=%d\n", inputMgr.hasTouch() ? 1 : 0);
}

void HalGPIO::serviceTouchGestures() {
  touchPressedEvents = 0;
  touchReleasedEvents = 0;
  touchSwipeDirection = TouchSwipe::None;
  touchSwipeStartNx = 0.0f;
  touchSwipeStartNy = 0.0f;
  if (!inputMgr.hasTouch()) return;

  // The shared SDK classifier uses a 60 px swipe threshold. That is reasonable
  // for the larger panels, but edge gestures on Picco's smaller 480x800 glass
  // are commonly shorter. Keep the SDK result when available and only add a
  // Picco-local fallback for a completed single-contact gesture.
  if (inputMgr.isTouchPressed()) {
    const InputManager::TouchPoint point = inputMgr.getTouchPoint();
    if (point.valid) {
      if (!touchGestureActive) {
        touchGestureStart = point;
        touchGestureActive = true;
      }
      touchGestureLast = point;
    }
  }

  float sx = 0.0f, sy = 0.0f, ex = 0.0f, ey = 0.0f;
  bool swipe = inputMgr.wasSwipe(sx, sy, ex, ey);
  if (!swipe && inputMgr.wasTouchReleased() && touchGestureActive) {
    const int dx = static_cast<int>(touchGestureLast.x) - static_cast<int>(touchGestureStart.x);
    const int dy = static_cast<int>(touchGestureLast.y) - static_cast<int>(touchGestureStart.y);
    const int dominant = std::max(std::abs(dx), std::abs(dy));
    if (dominant >= kPiccoSwipeMinPx &&
        millis() - touchGestureStart.timestamp <= kPiccoSwipeMaxMs) {
      const auto& touch = BoardConfig::ACTIVE.touch;
      sx = touch.rawMaxX > 0 ? static_cast<float>(touchGestureStart.x) / touch.rawMaxX : 0.0f;
      sy = touch.rawMaxY > 0 ? static_cast<float>(touchGestureStart.y) / touch.rawMaxY : 0.0f;
      ex = touch.rawMaxX > 0 ? static_cast<float>(touchGestureLast.x) / touch.rawMaxX : 0.0f;
      ey = touch.rawMaxY > 0 ? static_cast<float>(touchGestureLast.y) / touch.rawMaxY : 0.0f;
      swipe = true;
      INX_SERIAL.printf("[INPUT] Picco short-swipe fallback dx=%d dy=%d\n", dx, dy);
    }
  }
  if (inputMgr.wasTouchReleased()) touchGestureActive = false;
  if (!swipe) return;
  touchSwipeStartNx = sx;
  touchSwipeStartNy = sy;
  const float dx = ex - sx;
  const float dy = ey - sy;
  if (std::fabs(dy) > std::fabs(dx)) {
    touchSwipeDirection = dy < 0.0f ? TouchSwipe::Up : TouchSwipe::Down;
  } else {
    touchSwipeDirection = dx < 0.0f ? TouchSwipe::Left : TouchSwipe::Right;
  }
  // Picco's digitizer is rotated relative to the portrait UI. A portrait
  // top/bottom swipe is therefore horizontal in the native frame; converting
  // every native edge swipe into a synthetic Back press would preempt the
  // mapped drawer gestures in UiPage::menuInput(). Keep the mapped swipe as
  // the single source of truth for touch navigation.
  LOG_INF("INPUT", "Picco swipe raw=%s start=%.3f,%.3f end=%.3f,%.3f", touchSwipeDirection == TouchSwipe::Up ? "up" :
                                                                              touchSwipeDirection == TouchSwipe::Down ? "down" :
                                                                              touchSwipeDirection == TouchSwipe::Left ? "left" : "right",
          sx, sy, ex, ey);
}

void HalGPIO::update() {
  inputMgr.update();
  const bool touchPressed = inputMgr.isTouchPressed();
  if (touchPressed != lastTouchPressed) {
    float nx = 0.0f;
    float ny = 0.0f;
    if (touchPressed && inputMgr.wasTouchPressedAt(nx, ny)) {
      LOG_INF("INPUT", "Picco touch pressed nx=%.3f ny=%.3f", nx, ny);
    } else {
      LOG_INF("INPUT", "Picco touch released");
    }
    lastTouchPressed = touchPressed;
  }
  serviceTouchGestures();
}

void HalGPIO::injectOneShotPress(uint8_t) {}
bool HalGPIO::isPressed(const uint8_t buttonIndex) const { return inputMgr.isPressed(buttonIndex); }
bool HalGPIO::wasPressed(const uint8_t buttonIndex) const {
  return inputMgr.wasPressed(buttonIndex) || ((touchPressedEvents >> buttonIndex) & 1U);
}
bool HalGPIO::wasAnyPressed() const { return inputMgr.wasAnyPressed() || touchPressedEvents != 0; }
bool HalGPIO::wasReleased(const uint8_t buttonIndex) const {
  return inputMgr.wasReleased(buttonIndex) || ((touchReleasedEvents >> buttonIndex) & 1U);
}
bool HalGPIO::wasAnyReleased() const { return inputMgr.wasAnyReleased() || touchReleasedEvents != 0; }
unsigned long HalGPIO::getHeldTime() const { return inputMgr.getHeldTime(); }
bool HalGPIO::hasToggleSwitch() const { return inputMgr.hasToggleSwitch(); }
bool HalGPIO::isToggleSwitchOn() const { return inputMgr.isToggleSwitchOn(); }
bool HalGPIO::wasToggleSwitchChanged() const { return inputMgr.wasToggleSwitchChanged(); }
bool HalGPIO::hasTouch() const { return inputMgr.hasTouch(); }
bool HalGPIO::isTouchPressed() const { return inputMgr.isTouchPressed(); }
bool HalGPIO::wasTouchPressedAt(float& nx, float& ny) const { return inputMgr.wasTouchPressedAt(nx, ny); }
bool HalGPIO::isTouchHeldAt(float& nx, float& ny) const { return inputMgr.isTouchHeldAt(nx, ny); }
bool HalGPIO::wasTouchActivity() const { return inputMgr.wasTouchActivity(); }
bool HalGPIO::wasTouchTap(float& nx, float& ny) const { return inputMgr.wasTouchTap(nx, ny); }
unsigned long HalGPIO::lastTouchHeldMs() const { return inputMgr.lastTouchHeldMs(); }

bool HalGPIO::touchSwipeStart(float& nx, float& ny) const {
  if (touchSwipeDirection == TouchSwipe::None) return false;
  nx = touchSwipeStartNx;
  ny = touchSwipeStartNy;
  return true;
}

HalGPIO::MotionGesture HalGPIO::readMotionGesture(const uint8_t orientation, const uint8_t mode,
                                                   const uint8_t sensitivity) {
  if (mode == 0 && !motionSensorInitialized) return MotionGesture::None;
  const unsigned long now = millis();
  if (motionLastPollMs != 0 && now - motionLastPollMs < 50) return MotionGesture::None;
  motionLastPollMs = now;
  if (!motionSensorInitialized) {
    if (!imu().begin()) return MotionGesture::None;
    motionSensorInitialized = true;
    motionSensorStartedMs = now;
    motionLastGestureMs = now;
  }
  if (mode == 0) {
    imu().sleep();
    motionSensorInitialized = false;
    motionGestureInProgress = false;
    return MotionGesture::None;
  }
  freeink::Imu::Sample sample{};
  if (!imu().read(sample) || now - motionSensorStartedMs < 300) return MotionGesture::None;
  float axis = 0.0f;
  switch (orientation) {
    case 1: axis = -sample.gy; break;
    case 2: axis = -sample.gx; break;
    case 3: axis = sample.gy; break;
    default: axis = sample.gx; break;
  }
  if (mode == 2) axis = -axis;
  if (motionGestureInProgress) {
    if (std::fabs(axis) < 50.0f) motionGestureInProgress = false;
    return MotionGesture::None;
  }
  if (now - motionLastGestureMs < 600) return MotionGesture::None;
  const float trigger = sensitivity >= 2 ? 180.0f : sensitivity == 0 ? 360.0f : 270.0f;
  if (axis > trigger) {
    motionGestureInProgress = true;
    motionLastGestureMs = now;
    return MotionGesture::Next;
  }
  if (axis < -trigger) {
    motionGestureInProgress = true;
    motionLastGestureMs = now;
    return MotionGesture::Previous;
  }
  return MotionGesture::None;
}

void HalGPIO::startDeepSleep() {
  const auto& power = BoardConfig::ACTIVE.power;
  if (power.latch0 >= 0) {
    const auto pin = static_cast<gpio_num_t>(power.latch0);
    gpio_hold_dis(pin);
    pinMode(power.latch0, OUTPUT);
    digitalWrite(power.latch0, HIGH);
    gpio_hold_en(pin);
  }
  freeink::PowerManager::powerDownRailsForSleep();
  freeink::PowerManager::deepSleepUntilPowerButton();
}

int HalGPIO::getBatteryPercentage() const {
  const unsigned long now = millis();
  if (batteryLastPollMs != 0 && now - batteryLastPollMs < BATTERY_POLL_MS) return batteryCachedPercent;
  uint16_t percent = 0;
  static const BatteryMonitor battery;
  if (battery.readPercentageChecked(percent)) batteryCachedPercent = percent;
  batteryLastPollMs = now;
  return batteryCachedPercent;
}

bool HalGPIO::isCharging() const {
  const int8_t pin = BoardConfig::ACTIVE.batteryChargeStatus;
  if (pin < 0) return false;
  static bool configured = false;
  if (!configured) {
    pinMode(pin, INPUT_PULLUP);
    configured = true;
  }
  return digitalRead(pin) == LOW;
}

bool HalGPIO::isUsbConnected() const {
  const int8_t pin = BoardConfig::ACTIVE.usbDetect;
  return pin >= 0 && digitalRead(pin) == HIGH;
}

bool HalGPIO::readDateTime(DateTime& outDateTime) const {
  freeink::Rtc rtc;
  if (!rtc.begin()) return false;
  freeink::Rtc::DateTime dt;
  if (!rtc.now(dt)) return false;
  outDateTime.year = dt.year;
  outDateTime.month = dt.month;
  outDateTime.day = dt.day;
  outDateTime.hour = dt.hour;
  outDateTime.minute = dt.minute;
  outDateTime.second = dt.second;
  outDateTime.weekday = weekdayFromCalendarDate(dt.year, dt.month, dt.day);
  return true;
}

bool HalGPIO::writeDateTime(const DateTime& dateTime) const {
  freeink::Rtc rtc;
  if (!rtc.begin()) return false;
  freeink::Rtc::DateTime dt;
  dt.year = dateTime.year;
  dt.month = dateTime.month;
  dt.day = dateTime.day;
  dt.hour = dateTime.hour;
  dt.minute = dateTime.minute;
  dt.second = dateTime.second;
  dt.weekday = static_cast<uint8_t>(dateTime.weekday == 7 ? 0 : dateTime.weekday);
  return rtc.set(dt);
}

bool HalGPIO::syncRtcFromSystemTime() const {
  const time_t now = time(nullptr);
  if (now < 1704067200) return false;
  struct tm localTime {};
  if (!localtime_r(&now, &localTime)) return false;
  DateTime dt;
  dt.year = static_cast<uint16_t>(localTime.tm_year + 1900);
  dt.month = static_cast<uint8_t>(localTime.tm_mon + 1);
  dt.day = static_cast<uint8_t>(localTime.tm_mday);
  dt.hour = static_cast<uint8_t>(localTime.tm_hour);
  dt.minute = static_cast<uint8_t>(localTime.tm_min);
  dt.second = static_cast<uint8_t>(localTime.tm_sec);
  dt.weekday = static_cast<uint8_t>(localTime.tm_wday == 0 ? 7 : localTime.tm_wday);
  return writeDateTime(dt);
}

HalGPIO::WakeupReason HalGPIO::getWakeupReason() const {
  const auto cause = esp_sleep_get_wakeup_cause();
  const auto reset = esp_reset_reason();
  if (reset == ESP_RST_DEEPSLEEP && (cause == ESP_SLEEP_WAKEUP_GPIO || cause == ESP_SLEEP_WAKEUP_EXT1))
    return WakeupReason::PowerButton;
  if (cause == ESP_SLEEP_WAKEUP_UNDEFINED && reset == ESP_RST_POWERON && !isUsbConnected())
    return WakeupReason::PowerButton;
  if (cause == ESP_SLEEP_WAKEUP_UNDEFINED && reset == ESP_RST_UNKNOWN && isUsbConnected())
    return WakeupReason::AfterFlash;
  if (cause == ESP_SLEEP_WAKEUP_UNDEFINED && reset == ESP_RST_POWERON && isUsbConnected())
    return WakeupReason::AfterUSBPower;
  return WakeupReason::Other;
}

bool HalGPIO::getTemperatureAndHumidity(float&, float&) const { return false; }
