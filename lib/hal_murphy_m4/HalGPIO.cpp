/**
 * @file HalGPIO.cpp
 * @brief Murphy M4 variant of HalGPIO — delegates to freeink-sdk.
 */

#include <BatteryMonitor.h>
#include <BoardConfig.h>
#include <HalGPIO.h>
#include <PowerManager.h>
#include <driver/gpio.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <sys/time.h>

#include <cmath>
#include <cstdint>
#include <ctime>

#include "state/SystemSetting.h"

namespace {
constexpr time_t kMinimumValidEpoch = 1704067200;  // 2024-01-01 00:00:00 UTC

// Murphy M4 exposes the power key on the same physical GPIO as the legacy
// confirm input. Treat that shared electrical signal as power only; touch is
// the confirm mechanism on this device, so a power press must never activate
// a UI Confirm action.
bool isSharedConfirmButton(const uint8_t buttonIndex) {
  return buttonIndex == HalGPIO::BTN_CONFIRM;
}

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

int64_t daysFromCivil(const int year, const unsigned month, const unsigned day) {
  const int adjustedYear = year - (month <= 2 ? 1 : 0);
  const int era = (adjustedYear >= 0 ? adjustedYear : adjustedYear - 399) / 400;
  const unsigned yearOfEra = static_cast<unsigned>(adjustedYear - era * 400);
  const unsigned dayOfYear = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  const unsigned dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
  return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(dayOfEra) - 719468;
}

bool setSystemClockFromRtc(const MurphyRtc::DateTime& dateTime) {
  const int64_t localEpoch = daysFromCivil(dateTime.year, dateTime.month, dateTime.day) * 86400LL +
                             static_cast<int64_t>(dateTime.hour) * 3600LL +
                             static_cast<int64_t>(dateTime.minute) * 60LL + dateTime.second;
  const time_t utcEpoch = static_cast<time_t>(localEpoch -
                                               static_cast<int64_t>(SETTINGS.getTimeZoneOffsetMinutes()) * 60LL);
  timeval tv{};
  tv.tv_sec = utcEpoch;
  tv.tv_usec = 0;
  return settimeofday(&tv, nullptr) == 0;
}

}

void HalGPIO::begin() {
  INX_SERIAL.begin(115200);
  INX_SERIAL.printf("[MURPHY_M4][BOOT] reset_reason=%d\n", static_cast<int>(esp_reset_reason()));

  BoardConfig::holdPowerRails();

  // GPIO9 is the Murphy battery sense input. Set its attenuation explicitly
  // before BatteryMonitor creates its ADC channel; otherwise the first ADC
  // calibration can inherit a different attenuation from another peripheral.
  if (BoardConfig::ACTIVE.batteryAdc >= 0) {
    pinMode(BoardConfig::ACTIVE.batteryAdc, INPUT);
    analogSetPinAttenuation(BoardConfig::ACTIVE.batteryAdc, ADC_11db);
  }

  inputMgr.begin();
  touchDriver.begin();
  rtcReady = rtc.begin();
  INX_SERIAL.printf("[MURPHY_M4][RTC] RX8010 ready=%d bus=I2C1 sda=%d scl=%d addr=0x32\n", rtcReady ? 1 : 0,
                    BoardConfig::ACTIVE.touch.sda, BoardConfig::ACTIVE.touch.scl);
  if (rtcReady) {
    MurphyRtc::DateTime rtcDateTime;
    const bool clockValid = rtc.now(rtcDateTime);
    const bool clockSet = clockValid && setSystemClockFromRtc(rtcDateTime);
    INX_SERIAL.printf("[MURPHY_M4][RTC] RX8010 system clock %s\n", clockSet ? "restored" : "not restored");
  }
  INX_SERIAL.printf("[MURPHY_M4] touch controller ready=%d ranges=x:%u..%u y:%u..%u\n", touchDriver.hasTouch() ? 1 : 0,
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
    if (!touchDriver.isPressed()) suppressTouchUntilRelease = false;
    return;
  }
  if (!touchDriver.hasTouch()) {
    return;
  }

  float sx = 0.0f, sy = 0.0f, ex = 0.0f, ey = 0.0f;
  if (touchDriver.wasSwipe(sx, sy, ex, ey)) {
    touchSwipeStartNx = sx;
    touchSwipeStartNy = sy;
    suppressPostSwipeTap = true;
    swipeDetectedAtMs = millis();
    const float dx = ex - sx;
    const float dy = ey - sy;
    if (std::fabs(dy) > std::fabs(dx)) {
      touchSwipeDirection = dy < 0.0f ? TouchSwipe::Up : TouchSwipe::Down;
      INX_SERIAL.printf("[MURPHY_M4][TOUCH] SWIPE %s\n", dy < 0.0f ? "up" : "down");

      return;
    }

    touchSwipeDirection = dx < 0.0f ? TouchSwipe::Left : TouchSwipe::Right;
    INX_SERIAL.printf("[MURPHY_M4][TOUCH] SWIPE %s\n", dx < 0.0f ? "left" : "right");
  }
}

void HalGPIO::update() {
  if (touchSwipeLatchedDuringDisplay) {
    // Preserve the swipe detected by the BUSY-wait poll for the first normal
    // input cycle after the waveform completes.
    touchSwipeLatchedDuringDisplay = false;
    return;
  }
  // Start a fresh query window. All callers in the previous release frame
  // must observe the same suppressed result.
  postSwipeTapBlockedThisFrame = false;
  inputMgr.update();
  touchDriver.update();
  float pressedNx = 0.0f;
  float pressedNy = 0.0f;
  const bool touchPressed = touchDriver.wasPressedAt(pressedNx, pressedNy);
  if (touchPressed) {
    INX_SERIAL.printf("[MURPHY_M4][TOUCH] DOWN native=(%.3f,%.3f)\n", pressedNx, pressedNy);
  }
  if (touchPressed) {
    const MurphyTouchDriver::TouchPoint point = touchDriver.point();
    INX_SERIAL.printf("[MURPHY_M4][TOUCH] point=(%u,%u) valid=%d\n", point.x, point.y, point.valid ? 1 : 0);
  }
  if (touchDriver.wasActivity() && !touchDriver.isPressed()) {
    float tapNx = 0.0f;
    float tapNy = 0.0f;
    const bool isTap = touchDriver.wasTap(tapNx, tapNy);
    INX_SERIAL.printf("[MURPHY_M4][TOUCH] UP tap=%d native=(%.3f,%.3f) held=%lu\n", isTap ? 1 : 0, tapNx, tapNy,
                   touchDriver.lastHeldMs());
  }
  serviceTouchGestures();
}

void HalGPIO::updateDuringDisplay() {
  if (touchSwipeLatchedDuringDisplay) return;
  update();
  touchSwipeLatchedDuringDisplay = touchSwipeDirection != TouchSwipe::None;
}

bool HalGPIO::hasTouch() const { return touchDriver.hasTouch(); }

bool HalGPIO::isTouchPressed() const { return touchDriver.isPressed(); }

bool HalGPIO::wasTouchPressedAt(float& nx, float& ny) const { return touchDriver.wasPressedAt(nx, ny); }

bool HalGPIO::isTouchHeldAt(float& nx, float& ny) const { return touchDriver.isHeldAt(nx, ny); }

bool HalGPIO::wasTouchActivity() const { return touchDriver.wasActivity(); }

bool HalGPIO::wasTouchTap(float& nx, float& ny) const {
  if (suppressTouchUntilRelease || touchDriver.isPressed() || postSwipeTapBlockedThisFrame) return false;
  const bool tapped = touchDriver.wasTap(nx, ny);
  if (tapped && suppressPostSwipeTap) {
    // Murphy can remain in the panel BUSY interval after a swipe. A duplicate
    // short contact may arrive after that interval and must not become a tap.
    constexpr unsigned long kPostSwipeTapWindowMs = 3000;
    constexpr unsigned long kPostSwipeTapMaxHoldMs = 300;
    const unsigned long elapsed = millis() - swipeDetectedAtMs;
    const unsigned long heldMs = touchDriver.lastHeldMs();
    const bool shortTap = heldMs <= kPostSwipeTapMaxHoldMs;
    suppressPostSwipeTap = false;
    if (elapsed <= kPostSwipeTapWindowMs && shortTap) {
      postSwipeTapBlockedThisFrame = true;
      INX_SERIAL.printf("[MURPHY_M4] suppressed post-swipe tap nx=%.3f ny=%.3f held=%lums elapsed=%lums\n", nx, ny,
                        heldMs, elapsed);
      return false;
    }
  }
  if (tapped) {
    INX_SERIAL.printf("[MURPHY_M4] wasTouchTap() -> nx=%.3f ny=%.3f\n", nx, ny);
  }
  return tapped;
}

bool HalGPIO::touchSwipeStart(float& nx, float& ny) const {
  if (touchSwipeDirection == TouchSwipe::None) return false;
  nx = touchSwipeStartNx;
  ny = touchSwipeStartNy;
  return true;
}

unsigned long HalGPIO::lastTouchHeldMs() const { return touchDriver.lastHeldMs(); }

void HalGPIO::injectOneShotPress(uint8_t) {
}

bool HalGPIO::isPressed(uint8_t buttonIndex) const {
  if (isSharedConfirmButton(buttonIndex)) return false;
  return inputMgr.isPressed(buttonIndex);
}

bool HalGPIO::wasPressed(uint8_t buttonIndex) const {
  if (isSharedConfirmButton(buttonIndex)) return false;
  return inputMgr.wasPressed(buttonIndex) || ((touchPressedEvents >> buttonIndex) & 1);
}

bool HalGPIO::wasAnyPressed() const { return inputMgr.wasAnyPressed() || touchPressedEvents != 0; }

bool HalGPIO::wasReleased(uint8_t buttonIndex) const {
  if (isSharedConfirmButton(buttonIndex)) return false;
  return inputMgr.wasReleased(buttonIndex) || ((touchReleasedEvents >> buttonIndex) & 1);
}

bool HalGPIO::wasAnyReleased() const { return inputMgr.wasAnyReleased() || touchReleasedEvents != 0; }

unsigned long HalGPIO::getHeldTime() const { return inputMgr.getHeldTime(); }

void HalGPIO::ignoreCurrentTouchUntilRelease() {
  if (!touchDriver.isPressed()) return;
  touchDriver.suppressCurrentContact();
  suppressTouchUntilRelease = true;
  touchSwipeDirection = TouchSwipe::None;
  touchPressedEvents = 0;
  touchReleasedEvents = 0;
}

void HalGPIO::discardTouchSwipe() {
  touchSwipeDirection = TouchSwipe::None;
  // Keep the one-shot post-swipe tap guard across the activity transition.
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
  const auto& power = BoardConfig::ACTIVE.power;
  // Keep the battery and peripheral rails latched while deep sleep isolates GPIOs.
  for (const int8_t pin : {power.latch0, power.latch1}) {
    if (pin < 0) continue;
    const auto gpioPin = static_cast<gpio_num_t>(pin);
    gpio_hold_dis(gpioPin);
    pinMode(pin, OUTPUT);
    digitalWrite(pin, HIGH);
    gpio_hold_en(gpioPin);
  }
  // The caller has already put the panel to sleep and shut down mounted storage.
  freeink::PowerManager::powerDownRailsForSleep();
  freeink::PowerManager::deepSleepUntilPowerButton();
}

int HalGPIO::getBatteryPercentage() const {
  const unsigned long now = millis();
  if (batteryLastPollMs != 0 && (now - batteryLastPollMs) < BATTERY_POLL_MS) {
    return batteryCachedPercent < 0 ? 0 : batteryCachedPercent;
  }

  static const BatteryMonitor battery;
  uint16_t percent = 0;
  if (battery.readPercentageChecked(percent)) {
    batteryCachedPercent = percent;
  }
  batteryLastPollMs = now;
  return batteryCachedPercent < 0 ? 0 : batteryCachedPercent;
}

bool HalGPIO::isCharging() const {
  return isUsbConnected();
}

bool HalGPIO::isUsbConnected() const {
  if (BoardConfig::ACTIVE.usbDetect < 0) {
    // Murphy has no separate VBUS/charger-status GPIO. Native USB CDC is the
    // only reliable runtime indication available to the firmware; this is also
    // the connection used by the web installer.
    return HWCDC::isPlugged();
  }
  return digitalRead(BoardConfig::ACTIVE.usbDetect) == HIGH;
}

bool HalGPIO::readDateTime(DateTime& outDateTime) const {
  if (!rtcReady) rtcReady = rtc.begin();
  if (!rtcReady) return false;
  MurphyRtc::DateTime rtcDateTime;
  if (!rtc.now(rtcDateTime)) return false;
  outDateTime.year = rtcDateTime.year;
  outDateTime.month = rtcDateTime.month;
  outDateTime.day = rtcDateTime.day;
  outDateTime.hour = rtcDateTime.hour;
  outDateTime.minute = rtcDateTime.minute;
  outDateTime.second = rtcDateTime.second;
  outDateTime.weekday = weekdayFromCalendarDate(rtcDateTime.year, rtcDateTime.month, rtcDateTime.day);
  return true;
}

bool HalGPIO::writeDateTime(const DateTime& dateTime) const {
  if (dateTime.year < 2000 || dateTime.month < 1 || dateTime.month > 12 || dateTime.day < 1 || dateTime.day > 31 ||
      dateTime.hour > 23 || dateTime.minute > 59 || dateTime.second > 59) {
    return false;
  }

  MurphyRtc::DateTime rtcDateTime;
  rtcDateTime.year = dateTime.year;
  rtcDateTime.month = dateTime.month;
  rtcDateTime.day = dateTime.day;
  rtcDateTime.hour = dateTime.hour;
  rtcDateTime.minute = dateTime.minute;
  rtcDateTime.second = dateTime.second;
  rtcDateTime.weekday = static_cast<uint8_t>(dateTime.weekday == 7 ? 0 : dateTime.weekday);
  if (!rtcReady) rtcReady = rtc.begin();
  const bool written = rtcReady && rtc.set(rtcDateTime);
  INX_SERIAL.printf("[%lu] [MURPHY_M4][RTC] RX8010 %s\n", millis(), written ? "set" : "write failed");
  return written;
}

bool HalGPIO::syncRtcFromSystemTime() const {
  const time_t now = time(nullptr);
  if (now < kMinimumValidEpoch) return false;
  struct tm localTime {};
  if (localtime_r(&now, &localTime) == nullptr) return false;

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
