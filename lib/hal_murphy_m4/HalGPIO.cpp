/**
 * @file HalGPIO.cpp
 * @brief Murphy M4 variant of HalGPIO — delegates to freeink-sdk.
 */

#include <BatteryMonitor.h>
#include <BoardConfig.h>
#include <HalGPIO.h>
#include <PowerManager.h>
#include <driver/gpio.h>
#include <esp_rtc_time.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <Preferences.h>
#include <sys/time.h>

#include <cmath>
#include <cstdint>
#include <ctime>

#include "state/SystemSetting.h"

namespace {
constexpr time_t kMinimumValidEpoch = 1704067200;  // 2024-01-01 00:00:00 UTC
constexpr uint32_t kClockMagic = 0x4D525443;       // "MRTC"
constexpr char kClockPreferencesNamespace[] = "murphy-clock";
constexpr char kClockPreferencesKey[] = "wall-clock";

struct PersistedClock {
  uint32_t magic = 0;
  uint32_t generation = 0;
  int64_t epochUs = 0;
  uint64_t rtcUs = 0;
};

// This is the ESP32-S3's actual RTC timer state. The wall-clock sample is
// persisted separately because the timer measures elapsed time, not a date.
RTC_DATA_ATTR uint32_t rtcGenerationMagic = 0;
RTC_DATA_ATTR uint32_t rtcGeneration = 0;
RTC_DATA_ATTR PersistedClock rtcBaseline;

// Murphy M4 exposes the power key on the same physical GPIO as the legacy
// confirm input. Treat that shared electrical signal as power only; touch is
// the confirm mechanism on this device, so a power press must never activate
// a UI Confirm action.
bool isSharedConfirmButton(const uint8_t buttonIndex) {
  return buttonIndex == HalGPIO::BTN_CONFIRM;
}

void initializeRtcGeneration() {
  if (rtcGenerationMagic == kClockMagic && rtcGeneration != 0) return;
  rtcGeneration = esp_random();
  if (rtcGeneration == 0) rtcGeneration = 1;
  rtcGenerationMagic = kClockMagic;
  rtcBaseline = {};
}

bool loadPersistedClock(PersistedClock& clock) {
  Preferences preferences;
  if (!preferences.begin(kClockPreferencesNamespace, true)) return false;
  const size_t length = preferences.getBytesLength(kClockPreferencesKey);
  const bool loaded = length == sizeof(PersistedClock) &&
                      preferences.getBytes(kClockPreferencesKey, &clock, sizeof(PersistedClock)) == sizeof(PersistedClock);
  preferences.end();
  return loaded && clock.magic == kClockMagic && clock.epochUs > 0;
}

bool savePersistedClock(const PersistedClock& clock) {
  Preferences preferences;
  if (!preferences.begin(kClockPreferencesNamespace, false)) return false;
  const size_t written = preferences.putBytes(kClockPreferencesKey, &clock, sizeof(PersistedClock));
  preferences.end();
  return written == sizeof(PersistedClock);
}

bool persistCurrentClock() {
  timeval nowTv{};
  if (gettimeofday(&nowTv, nullptr) != 0 || nowTv.tv_sec < kMinimumValidEpoch) return false;

  rtcBaseline.magic = kClockMagic;
  rtcBaseline.generation = rtcGeneration;
  rtcBaseline.epochUs = static_cast<int64_t>(nowTv.tv_sec) * 1000000LL + nowTv.tv_usec;
  rtcBaseline.rtcUs = esp_rtc_get_time_us();
  const bool saved = savePersistedClock(rtcBaseline);
  INX_SERIAL.printf("[%lu] [MURPHY_M4][CLOCK] stock RTC baseline %s epoch=%lld\n", millis(),
                    saved ? "saved" : "save failed", static_cast<long long>(nowTv.tv_sec));
  return saved;
}

bool restoreClockFromRtc() {
  if (time(nullptr) >= kMinimumValidEpoch) return true;

  PersistedClock clock = rtcBaseline;
  const bool retained = clock.magic == kClockMagic && clock.generation == rtcGeneration && clock.epochUs > 0 &&
                        clock.rtcUs > 0;
  if (!retained && !loadPersistedClock(clock)) {
    INX_SERIAL.printf("[%lu] [MURPHY_M4][CLOCK] no persisted wall clock\n", millis());
    return false;
  }

  // The persisted sample is only valid inside the same ESP32 RTC power epoch.
  // Do not replay a date from before a full power loss; stock MurphyOS waits
  // for NTP or a manual correction in that case.
  if (clock.magic != kClockMagic || clock.generation != rtcGeneration || clock.epochUs <= 0 || clock.rtcUs == 0) {
    INX_SERIAL.printf("[%lu] [MURPHY_M4][CLOCK] persisted wall clock rejected: generation baseline=%u current=%u\n",
                      millis(), clock.generation, rtcGeneration);
    return false;
  }

  const uint64_t rtcNowUs = esp_rtc_get_time_us();
  int64_t recoveredEpochUs = clock.epochUs;
  if (rtcNowUs < clock.rtcUs) {
    INX_SERIAL.printf("[%lu] [MURPHY_M4][CLOCK] persisted wall clock rejected: rtc moved backwards\n", millis());
    return false;
  }
  recoveredEpochUs += static_cast<int64_t>(rtcNowUs - clock.rtcUs);

  timeval tv{};
  tv.tv_sec = static_cast<time_t>(recoveredEpochUs / 1000000LL);
  tv.tv_usec = static_cast<suseconds_t>(recoveredEpochUs % 1000000LL);
  if (settimeofday(&tv, nullptr) != 0 || time(nullptr) < kMinimumValidEpoch) {
    INX_SERIAL.printf("[%lu] [MURPHY_M4][CLOCK] stock RTC recovery failed\n", millis());
    return false;
  }

  INX_SERIAL.printf("[%lu] [MURPHY_M4][CLOCK] stock RTC restored epoch=%lld source=retained_rtc\n", millis(),
                    static_cast<long long>(time(nullptr)));
  return true;
}

// Convert a Gregorian calendar date to Unix days without depending on the
// process TZ/mktime state. The reader stores the wall clock in UTC and applies
// the configured offset only when presenting it to the user.
int64_t daysFromCivil(const int year, const unsigned month, const unsigned day) {
  const int adjustedYear = year - (month <= 2 ? 1 : 0);
  const int era = (adjustedYear >= 0 ? adjustedYear : adjustedYear - 399) / 400;
  const unsigned yearOfEra = static_cast<unsigned>(adjustedYear - era * 400);
  const unsigned dayOfYear = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  const unsigned dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
  return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(dayOfEra) - 719468;
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

}

void HalGPIO::begin() {
  INX_SERIAL.begin(115200);
  INX_SERIAL.printf("[MURPHY_M4][BOOT] reset_reason=%d\n", static_cast<int>(esp_reset_reason()));

  BoardConfig::holdPowerRails();
  initializeRtcGeneration();
  restoreClockFromRtc();

  // GPIO9 is the Murphy battery sense input. Set its attenuation explicitly
  // before BatteryMonitor creates its ADC channel; otherwise the first ADC
  // calibration can inherit a different attenuation from another peripheral.
  if (BoardConfig::ACTIVE.batteryAdc >= 0) {
    pinMode(BoardConfig::ACTIVE.batteryAdc, INPUT);
    analogSetPinAttenuation(BoardConfig::ACTIVE.batteryAdc, ADC_11db);
  }

  inputMgr.begin();
  touchDriver.begin();
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

bool HalGPIO::hasTouch() const { return touchDriver.hasTouch(); }

bool HalGPIO::isTouchPressed() const { return touchDriver.isPressed(); }

bool HalGPIO::wasTouchPressedAt(float& nx, float& ny) const { return touchDriver.wasPressedAt(nx, ny); }

bool HalGPIO::isTouchHeldAt(float& nx, float& ny) const { return touchDriver.isHeldAt(nx, ny); }

bool HalGPIO::wasTouchActivity() const { return touchDriver.wasActivity(); }

bool HalGPIO::wasTouchTap(float& nx, float& ny) const {
  if (suppressTouchUntilRelease) return false;
  const bool tapped = touchDriver.wasTap(nx, ny);
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
  time_t utcNow = time(nullptr);
  if (utcNow < kMinimumValidEpoch) {
    restoreClockFromRtc();
    utcNow = time(nullptr);
  }
  if (utcNow < kMinimumValidEpoch) return false;

  const time_t localEpoch = utcNow + static_cast<time_t>(SETTINGS.getTimeZoneOffsetMinutes()) * 60;
  struct tm localTime {};
  if (gmtime_r(&localEpoch, &localTime) == nullptr) return false;

  outDateTime.year = static_cast<uint16_t>(localTime.tm_year + 1900);
  outDateTime.month = static_cast<uint8_t>(localTime.tm_mon + 1);
  outDateTime.day = static_cast<uint8_t>(localTime.tm_mday);
  outDateTime.hour = static_cast<uint8_t>(localTime.tm_hour);
  outDateTime.minute = static_cast<uint8_t>(localTime.tm_min);
  outDateTime.second = static_cast<uint8_t>(localTime.tm_sec);
  outDateTime.weekday = static_cast<uint8_t>(localTime.tm_wday == 0 ? 7 : localTime.tm_wday);
  return true;
}

bool HalGPIO::writeDateTime(const DateTime& dateTime) const {
  if (dateTime.year < 2000 || dateTime.month < 1 || dateTime.month > 12 || dateTime.day < 1 || dateTime.day > 31 ||
      dateTime.hour > 23 || dateTime.minute > 59 || dateTime.second > 59) {
    return false;
  }

  const int64_t localEpoch = daysFromCivil(dateTime.year, dateTime.month, dateTime.day) * 86400LL +
                             static_cast<int64_t>(dateTime.hour) * 3600LL +
                             static_cast<int64_t>(dateTime.minute) * 60LL + dateTime.second;
  const time_t utcEpoch = static_cast<time_t>(localEpoch - static_cast<int64_t>(SETTINGS.getTimeZoneOffsetMinutes()) * 60LL);
  timeval tv{};
  tv.tv_sec = utcEpoch;
  tv.tv_usec = 0;
  const bool written = settimeofday(&tv, nullptr) == 0;
  if (written) persistCurrentClock();
  INX_SERIAL.printf("[%lu] [MURPHY_M4][CLOCK] stock RTC %s epoch=%lld\n", millis(), written ? "set" : "write failed",
                    static_cast<long long>(utcEpoch));
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
  return persistCurrentClock();
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
