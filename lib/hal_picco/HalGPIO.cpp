#include "HalGPIO.h"

#include <BatteryMonitor.h>
#include <BoardConfig.h>
#include <Imu.h>
#include <HWCDC.h>
#include <PowerManager.h>
#include <Rtc.h>
#include <Wire.h>
#include <driver/gpio.h>
#include <esp_sleep.h>
#include <esp_system.h>

#include "system/Logging.h"
#include "system/ClockTime.h"
#include "state/SystemSetting.h"
#include "PiccoCharger.h"

#include <cmath>
#include <ctime>


namespace {
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
  if (BoardConfig::ACTIVE.batteryAdc >= 0) pinMode(BoardConfig::ACTIVE.batteryAdc, INPUT);
  // Picco uses polled buttons. InputManager may report that the boot-time
  // power-button ISR service is absent while clearing its wake handler; do not
  // install the service here because ESP32-S3 performs that registration on
  // ipc0 and can overflow the tiny IPC task stack during early boot.
  inputMgr.begin();
  nextTouchRetryMs = millis() + 1000;
  INX_SERIAL.printf("[PICCO] input ready touch=%d ranges=x:%u..%u y:%u..%u\n", inputMgr.hasTouch() ? 1 : 0,
                    BoardConfig::ACTIVE.touch.rawMinX, BoardConfig::ACTIVE.touch.rawMaxX,
                    BoardConfig::ACTIVE.touch.rawMinY, BoardConfig::ACTIVE.touch.rawMaxY);
}

void HalGPIO::restoreTouchBusSettings() {
  // Arduino Wire.begin() reuses an already-started master bus. The frontlight
  // changes only its timeout; retain that bus and every initialized peripheral.
  Wire.setTimeOut(10);
  auto& config = BoardConfig::ACTIVE.batteryGauge;
  const uint8_t expectedAddress = config.chargerAddr;
  const uint8_t detectedAddress = picco_charger::findAddress(Wire, expectedAddress);
  if (detectedAddress != 0) {
    config.chargerAddr = detectedAddress;
    LOG_INF("PICCO-I2C", "charger identified addr=0x%02X profile_addr=0x%02X", detectedAddress, expectedAddress);
  } else {
    LOG_ERR("PICCO-I2C", "charger identity not found at factory addresses 0x01..0x07; profile_addr=0x%02X",
            expectedAddress);
  }
}

void HalGPIO::serviceTouchGestures() {
  touchPressedEvents = 0;
  touchReleasedEvents = 0;
  touchSwipeDirection = TouchSwipe::None;
  touchSwipeStartNx = 0.0f;
  touchSwipeStartNy = 0.0f;
  touchSwipeEndNx = 0.0f;
  touchSwipeEndNy = 0.0f;
  if (suppressTouchUntilRelease) {
    if (!inputMgr.isTouchPressed()) suppressTouchUntilRelease = false;
    return;
  }
  // Publish a Picco gesture only on the controller's release edge. Keeping
  // the latch out of the held-contact updates prevents a swipe from being
  // reinterpreted as a tap by a page that is rendered during the drag.
  if (!inputMgr.hasTouch() || !inputMgr.wasTouchReleased()) return;

  float sx = 0.0f, sy = 0.0f, ex = 0.0f, ey = 0.0f;
  if (!touchGesture.wasSwipe(sx, sy, ex, ey)) return;
  touchSwipeStartNx = sx;
  touchSwipeStartNy = sy;
  touchSwipeEndNx = ex;
  touchSwipeEndNy = ey;
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

void HalGPIO::updateDuringDisplay() {
  if (touchSwipeLatchedDuringDisplay) return;
  update();
  touchSwipeLatchedDuringDisplay = touchSwipeDirection != TouchSwipe::None;
}

void HalGPIO::update() {
  if (touchSwipeLatchedDuringDisplay) {
    touchSwipeLatchedDuringDisplay = false;
    return;
  }
  if (!inputMgr.hasTouch()) {
    const unsigned long now = millis();
    if (now >= nextTouchRetryMs) {
      nextTouchRetryMs = now + 1000;
      INX_SERIAL.println("[PICCO][TOUCH] controller not ready; retrying initialization");
      inputMgr.begin();
      if (inputMgr.hasTouch()) {
        lastTouchPressed = false;
        touchGesture = {};
        INX_SERIAL.println("[PICCO][TOUCH] initialization recovered");
      }
    }
  }
  inputMgr.update();
  const auto& touch = BoardConfig::ACTIVE.touch;
  touchGesture.update(inputMgr, touch.rawMaxX - touch.rawMinX, touch.rawMaxY - touch.rawMinY);
  const bool touchPressed = inputMgr.isTouchPressed();
  if (touchPressed != lastTouchPressed) {
    float nx = 0.0f;
    float ny = 0.0f;
    if (touchPressed && inputMgr.wasTouchPressedAt(nx, ny)) {
      const auto point = inputMgr.getTouchPoint();
      // Picco's app orientation is portrait: screen=(1-nativeY,nativeX).
      const float screenNx = 1.0f - ny;
      const float screenNy = nx;
      const auto& touch = BoardConfig::ACTIVE.touch;
      LOG_INF("INPUT",
              "Picco touch pressed native=(%.3f,%.3f) mapped=(%u,%u) screen=(%.3f,%.3f) rawRange=x:%u..%u y:%u..%u",
              nx, ny, point.x, point.y, screenNx, screenNy, touch.rawMinX, touch.rawMaxX, touch.rawMinY,
              touch.rawMaxY);
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
bool HalGPIO::wasTouchTap(float& nx, float& ny) const {
  if (suppressTouchUntilRelease || inputMgr.isTouchPressed() || !inputMgr.wasTouchReleased()) return false;
  return touchGesture.wasTap(nx, ny);
}
unsigned long HalGPIO::lastTouchHeldMs() const { return inputMgr.lastTouchHeldMs(); }

bool HalGPIO::touchSwipeStart(float& nx, float& ny) const {
  if (touchSwipeDirection == TouchSwipe::None) return false;
  nx = touchSwipeStartNx;
  ny = touchSwipeStartNy;
  return true;
}

bool HalGPIO::touchSwipeEnd(float& nx, float& ny) const {
  if (touchSwipeDirection == TouchSwipe::None) return false;
  nx = touchSwipeEndNx;
  ny = touchSwipeEndNy;
  return true;
}

void HalGPIO::ignoreCurrentTouchUntilRelease() {
  if (!inputMgr.isTouchPressed()) return;
  inputMgr.suppressTouchContact();
  touchGesture = {};
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

void HalGPIO::startDeepSleep(const bool wakeOnToggle) {
  const auto& power = BoardConfig::ACTIVE.power;
  if (power.latch0 >= 0) {
    const auto pin = static_cast<gpio_num_t>(power.latch0);
    gpio_hold_dis(pin);
    pinMode(power.latch0, OUTPUT);
    digitalWrite(power.latch0, HIGH);
    gpio_hold_en(pin);
  }
  freeink::PowerManager::powerDownRailsForSleep();
  if (wakeOnToggle && hasToggleSwitch() && !BoardConfig::ACTIVE.input.powerActiveHigh) {
    const int8_t powerPin = BoardConfig::ACTIVE.input.power;
    const int8_t togglePin = BoardConfig::ACTIVE.input.toggleSwitch;
    freeink::PowerManager::waitForPowerButtonRelease();
    pinMode(togglePin, INPUT_PULLUP);
    uint64_t wakeMask = 1ULL << togglePin;
    if (powerPin >= 0) {
      pinMode(powerPin, INPUT_PULLUP);
      wakeMask |= 1ULL << powerPin;
    }
    // Picco's power button and slide switch are both active-low. Wake from
    // either one so switching Power back on can boot the device.
    freeink::PowerManager::armWakeOnPins(wakeMask, true);
    INX_SERIAL.printf("[PICCO][POWER-SWITCH] deep sleep wake mask=0x%llX\n",
                      static_cast<unsigned long long>(wakeMask));
    freeink::PowerManager::deepSleep();
  }
  freeink::PowerManager::deepSleepUntilPowerButton();
}

void HalGPIO::pollBattery() const {
  const unsigned long now = millis();
  if (!batteryPoll.due(now)) return;
  static const BatteryMonitor battery;
  const auto sample = battery.readStatus();
  if (!sample.externalPowerKnown && !chargerProbeLogged) {
    // An empty write probes the address without modifying charger registers.
    // Wire maps a missing ACK to 2, distinguishing it from a failed register
    // read/driver state. Run once, never reset the bus in response to a failure.
    const auto& config = BoardConfig::ACTIVE.batteryGauge;
    Wire.beginTransmission(config.chargerAddr);
    const uint8_t probe = Wire.endTransmission();
    LOG_INF("PICCO-I2C", "charger=0x%02X probe=%u sda=%d scl=%d hz=%lu timeout_ms=%u pins=%d,%d",
            config.chargerAddr, probe, config.i2cSda, config.i2cScl,
            static_cast<unsigned long>(Wire.getClock()), Wire.getTimeOut(),
            gpio_get_level(static_cast<gpio_num_t>(config.i2cSda)),
            gpio_get_level(static_cast<gpio_num_t>(config.i2cScl)));
    chargerProbeLogged = true;
  }
  batteryState.update(sample, batteryPoll.elapsed(now));
  // Log once on startup, charger-state changes, or every 30 seconds. Include
  // validity flags: an unread charger is different from a disconnected cable.
  if (!batteryPoll.sampled() || sample.chargerStatus != batteryLastLoggedCharger || now - batteryLastLogMs >= 30000) {
    LOG_INF("PICCO-PWR", "mv=%u mv_ok=%d raw_pct=%u pct=%u reg08=%d usb=%d usb_ok=%d "
                         "charging=%d charge_ok=%d full=%d full_ok=%d",
            sample.millivolts, sample.millivoltsKnown, sample.percentage, batteryState.percentage(),
            sample.chargerStatus, sample.externalPower, sample.externalPowerKnown, sample.charging,
            sample.chargingKnown, sample.chargeComplete, sample.chargeCompleteKnown);
    batteryLastLogMs = now;
    batteryLastLoggedCharger = sample.chargerStatus;
  }
  batteryPoll.complete(now, sample.externalPowerKnown);
}

int HalGPIO::getBatteryPercentage() const {
  pollBattery();
  return batteryState.percentage();
}

bool HalGPIO::isCharging() const {
  pollBattery();
  return batteryState.isCharging();
}

bool HalGPIO::isUsbConnected() const {
  pollBattery();
  return batteryState.isUsbConnected(HWCDC::isPlugged());
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
  DateTime dt{};
  return clock_time::toRtcDateTime(time(nullptr), SETTINGS.getTimeZoneOffsetMinutes(), dt) && writeDateTime(dt);
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
