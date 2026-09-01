/**
 * @file HalGPIO.cpp
 * @brief Sticky variant of HalGPIO — delegates to freeink-sdk.
 */

#include <BatteryMonitor.h>
#include <BoardConfig.h>
#include <HalGPIO.h>
#include <Rtc.h>
#include <esp_sleep.h>
#include <esp_system.h>

#include <cmath>

namespace {
uint8_t weekdayFromCalendarDate(const uint16_t year, const uint8_t month, const uint8_t day) {
  if (year < 1900 || month < 1 || month > 12 || day < 1 || day > 31) {
    return 0;
  }
  static constexpr uint8_t monthOffsets[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  uint16_t adjustedYear = year;
  if (month < 3) --adjustedYear;
  const uint8_t sundayZero = static_cast<uint8_t>(
      (adjustedYear + adjustedYear / 4 - adjustedYear / 100 + adjustedYear / 400 + monthOffsets[month - 1] + day) % 7);
  return static_cast<uint8_t>(sundayZero == 0 ? 7 : sundayZero);
}

}

void HalGPIO::begin() {
  INX_SERIAL.begin(115200);
  INX_SERIAL.printf("[X4PRO][BOOT] reset_reason=%d\n", static_cast<int>(esp_reset_reason()));

  BoardConfig::holdPowerRails();

  inputMgr.begin();
  INX_SERIAL.printf("[X4PRO] touch controller ready=%d ranges=x:%u..%u y:%u..%u\n", inputMgr.hasTouch() ? 1 : 0,
                 BoardConfig::ACTIVE.touch.rawMinX, BoardConfig::ACTIVE.touch.rawMaxX,
                 BoardConfig::ACTIVE.touch.rawMinY, BoardConfig::ACTIVE.touch.rawMaxY);
}

void HalGPIO::serviceTouchGestures() {
  touchPressedEvents = 0;
  touchReleasedEvents = 0;
  touchSwipeDirection = TouchSwipe::None;
  touchSwipeStartNx = 0.0f;
  touchSwipeStartNy = 0.0f;
  if (suppressTouchUntilRelease) {
    if (!inputMgr.isTouchPressed()) suppressTouchUntilRelease = false;
    return;
  }
  if (!inputMgr.hasTouch()) {
    return;
  }

  if (inputMgr.wasHomeKeyPressed()) {
    touchPressedEvents |= (1 << BTN_BACK);
  }
  if (inputMgr.wasHomeKeyTapped()) {
    touchReleasedEvents |= (1 << BTN_BACK);
  }

  float sx = 0.0f, sy = 0.0f, ex = 0.0f, ey = 0.0f;
  if (inputMgr.wasSwipe(sx, sy, ex, ey)) {
    touchSwipeStartNx = sx;
    touchSwipeStartNy = sy;
    const float dx = ex - sx;
    const float dy = ey - sy;
    if (std::fabs(dy) > std::fabs(dx)) {
      touchSwipeDirection = dy < 0.0f ? TouchSwipe::Up : TouchSwipe::Down;
      INX_SERIAL.printf("[X4PRO][TOUCH] SWIPE %s\n", dy < 0.0f ? "up" : "down");

      return;
    }

    touchSwipeDirection = dx < 0.0f ? TouchSwipe::Left : TouchSwipe::Right;
    INX_SERIAL.printf("[X4PRO][TOUCH] SWIPE %s\n", dx < 0.0f ? "left" : "right");
  }
}

void HalGPIO::update() {
  inputMgr.update();
  float pressedNx = 0.0f;
  float pressedNy = 0.0f;
  if (inputMgr.wasTouchPressedAt(pressedNx, pressedNy)) {
    INX_SERIAL.printf("[X4PRO][TOUCH] DOWN native=(%.3f,%.3f)\n", pressedNx, pressedNy);
  }
  if (inputMgr.wasTouchPressed()) {
    const InputManager::TouchPoint point = inputMgr.getTouchPoint();
    INX_SERIAL.printf("[X4PRO][TOUCH] point=(%u,%u) valid=%d\n", point.x, point.y, point.valid ? 1 : 0);
  }
  if (inputMgr.wasTouchReleased()) {
    float tapNx = 0.0f;
    float tapNy = 0.0f;
    const bool isTap = inputMgr.wasTouchTap(tapNx, tapNy);
    INX_SERIAL.printf("[X4PRO][TOUCH] UP tap=%d native=(%.3f,%.3f) held=%lu\n", isTap ? 1 : 0, tapNx, tapNy,
                   inputMgr.lastTouchHeldMs());
  }
  serviceTouchGestures();
}

bool HalGPIO::hasTouch() const { return inputMgr.hasTouch(); }

bool HalGPIO::isTouchPressed() const { return inputMgr.isTouchPressed(); }

bool HalGPIO::wasTouchPressedAt(float& nx, float& ny) const { return inputMgr.wasTouchPressedAt(nx, ny); }

bool HalGPIO::isTouchHeldAt(float& nx, float& ny) const { return inputMgr.isTouchHeldAt(nx, ny); }

bool HalGPIO::wasTouchActivity() const { return inputMgr.wasTouchActivity(); }

bool HalGPIO::wasTouchTap(float& nx, float& ny) const {
  if (suppressTouchUntilRelease) return false;
  const bool tapped = inputMgr.wasTouchTap(nx, ny);
  if (tapped) {
    INX_SERIAL.printf("[X4PRO] wasTouchTap() -> nx=%.3f ny=%.3f\n", nx, ny);
  }
  return tapped;
}

bool HalGPIO::touchSwipeStart(float& nx, float& ny) const {
  if (touchSwipeDirection == TouchSwipe::None) return false;
  nx = touchSwipeStartNx;
  ny = touchSwipeStartNy;
  return true;
}

unsigned long HalGPIO::lastTouchHeldMs() const { return inputMgr.lastTouchHeldMs(); }

void HalGPIO::injectOneShotPress(uint8_t) {
}

bool HalGPIO::isPressed(uint8_t buttonIndex) const { return inputMgr.isPressed(buttonIndex); }

bool HalGPIO::wasPressed(uint8_t buttonIndex) const {
  return inputMgr.wasPressed(buttonIndex) || ((touchPressedEvents >> buttonIndex) & 1);
}

bool HalGPIO::wasAnyPressed() const { return inputMgr.wasAnyPressed() || touchPressedEvents != 0; }

bool HalGPIO::wasReleased(uint8_t buttonIndex) const {
  return inputMgr.wasReleased(buttonIndex) || ((touchReleasedEvents >> buttonIndex) & 1);
}

bool HalGPIO::wasAnyReleased() const { return inputMgr.wasAnyReleased() || touchReleasedEvents != 0; }

unsigned long HalGPIO::getHeldTime() const { return inputMgr.getHeldTime(); }

void HalGPIO::ignoreCurrentTouchUntilRelease() {
  if (!inputMgr.isTouchPressed()) return;
  suppressTouchUntilRelease = true;
  touchSwipeDirection = TouchSwipe::None;
  touchPressedEvents = 0;
  touchReleasedEvents = 0;
}

void HalGPIO::discardTouchSwipe() {
  touchSwipeDirection = TouchSwipe::None;
  touchPressedEvents = 0;
  touchReleasedEvents = 0;
}

HalGPIO::MotionGesture HalGPIO::readMotionGesture(const uint8_t orientation, const uint8_t mode,
                                                   const uint8_t sensitivity) {
  (void)orientation;
  (void)mode;
  (void)sensitivity;
  return MotionGesture::None;
}

void HalGPIO::startDeepSleep() {
  const auto& in = BoardConfig::ACTIVE.input;
  const bool pressedLevel = in.powerActiveHigh ? HIGH : LOW;
  while (digitalRead(in.power) == pressedLevel) {
    delay(50);
  }
  esp_sleep_enable_ext1_wakeup(1ULL << in.power, in.powerActiveHigh ? ESP_EXT1_WAKEUP_ANY_HIGH : ESP_EXT1_WAKEUP_ANY_LOW);
  esp_deep_sleep_start();
}

int HalGPIO::getBatteryPercentage() const {
  const unsigned long now = millis();
  if (batteryLastPollMs != 0 && (now - batteryLastPollMs) < BATTERY_POLL_MS) {
    return batteryCachedPercent;
  }

  static const BatteryMonitor battery;
  uint16_t percent = 0;
  if (battery.readPercentageChecked(percent)) {
    batteryCachedPercent = percent;
  }
  batteryLastPollMs = now;
  return batteryCachedPercent;
}

bool HalGPIO::isCharging() const {
  if (HWCDC::isPlugged()) return true;

  static const BatteryMonitor battery;
  return battery.isCharging();
}

bool HalGPIO::isUsbConnected() const {
  if (BoardConfig::ACTIVE.usbDetect < 0) {
    return false;
  }
  return digitalRead(BoardConfig::ACTIVE.usbDetect) == HIGH;
}

bool HalGPIO::readDateTime(DateTime& outDateTime) const {
  freeink::Rtc rtc;
  if (!rtc.begin()) {
    return false;
  }
  freeink::Rtc::DateTime dt;
  if (!rtc.now(dt)) {
    return false;
  }
  outDateTime.year = dt.year;
  outDateTime.month = dt.month;
  outDateTime.day = dt.day;
  outDateTime.hour = dt.hour;
  outDateTime.minute = dt.minute;
  outDateTime.second = dt.second;
  const uint8_t calendarWeekday = weekdayFromCalendarDate(dt.year, dt.month, dt.day);
  outDateTime.weekday = calendarWeekday != 0 ? calendarWeekday : static_cast<uint8_t>(dt.weekday == 0 ? 7 : dt.weekday);
  return true;
}

bool HalGPIO::writeDateTime(const DateTime& dateTime) const {
  freeink::Rtc rtc;
  if (!rtc.begin()) {
    return false;
  }
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
  if (now < 1704067200) {
    return false;
  }
  struct tm localTime {};
  if (localtime_r(&now, &localTime) == nullptr) {
    return false;
  }
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
  const auto wakeupCause = esp_sleep_get_wakeup_cause();
  const auto resetReason = esp_reset_reason();
  const bool usbConnected = isUsbConnected();

  if (resetReason == ESP_RST_DEEPSLEEP &&
      (wakeupCause == ESP_SLEEP_WAKEUP_GPIO || wakeupCause == ESP_SLEEP_WAKEUP_EXT1)) {
    return WakeupReason::PowerButton;
  }
  if (wakeupCause == ESP_SLEEP_WAKEUP_UNDEFINED && resetReason == ESP_RST_POWERON) {
    return WakeupReason::Other;
  }
  if (wakeupCause == ESP_SLEEP_WAKEUP_UNDEFINED && resetReason == ESP_RST_UNKNOWN && usbConnected) {
    return WakeupReason::AfterFlash;
  }
  if (wakeupCause == ESP_SLEEP_WAKEUP_UNDEFINED && resetReason == ESP_RST_POWERON && usbConnected) {
    return WakeupReason::AfterUSBPower;
  }
  return WakeupReason::Other;
}

bool HalGPIO::getTemperatureAndHumidity(float& outTempC, float& outHumidityPct) const {
  (void)outTempC;
  (void)outHumidityPct;
  return false;
}

constexpr float kBottomBandNy = 0.97f;
constexpr unsigned long kMinPhantomHoldMs = 600;

bool suppressBottomEdgeTap(const float ny, const unsigned long heldMs) {
  return ny >= kBottomBandNy && heldMs >= kMinPhantomHoldMs;
}
