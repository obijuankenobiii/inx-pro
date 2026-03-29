/**
 * @file HalGPIO.cpp
 * @brief Sticky variant of HalGPIO — delegates to freeink-sdk.
 */

#include <BatteryMonitor.h>
#include <BoardConfig.h>
#include <EnvironmentSensor.h>
#include <HalGPIO.h>
#include <Imu.h>
#include <Rtc.h>
#include <esp_sleep.h>
#include <esp_system.h>

#include <cmath>

namespace {
// Edge margin (normalized 0..1, panel-native frame) a swipe must start within
// to count as an edge-swipe-back gesture. Tuning guess, not hardware-verified.
constexpr float kEdgeSwipeMargin = 0.12f;

// The PCF8563 weekday register is auxiliary state. It can be stale or contain
// an invalid legacy value even though the RTC's calendar date is correct.
// Derive the UI weekday from that date instead: 1=Monday .. 7=Sunday.
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

freeink::Imu& sharedImu() {
  static freeink::Imu imu;
  return imu;
}
}  // namespace

void HalGPIO::begin() {
  // Sticky's USB-C goes through an on-board UART bridge to UART0 (INX_SERIAL), not the
  // S3's native USB CDC (main.cpp's INX_SERIAL.begin() only fires when
  // isUsbConnected() is true, and that's always false on Sticky by design —
  // see isUsbConnected() below). Without this, INX_SERIAL.printf() calls
  // anywhere in the app silently go nowhere: the ROM bootloader's own boot
  // banner prints at the hardware level regardless, so a monitor showing
  // *only* that banner and nothing from the app is this, not a hang.
  INX_SERIAL.begin(115200);
  INX_SERIAL.printf("[STICKY][BOOT] reset_reason=%d\n", static_cast<int>(esp_reset_reason()));

  // Sticky is battery-latched (PWR_HOLD/PWR_LOCK): without this first, the
  // board powers off as soon as the user releases the power button. Belongs
  // here, not in shared main.cpp — device power-on behavior is a HAL concern.
  BoardConfig::holdPowerRails();

  // BoardConfig::STICKY's touch.swapXY=true (freeink-sdk, upstream — not
  // hand-edited here) is flagged "pending hardware validation" in its own
  // comments, and real-hardware data confirms it's wrong for this unit: five
  // tab-bar icons laid out horizontally at a fixed Y produced nx pinned near
  // a fixed value and ny sweeping nearly the full 0..1 range — the axes are
  // swapped relative to what synthesizeConfirm/normal taps expect. ACTIVE is
  // mutable and read fresh on every wasTouchTap() call (see freeink-sdk's
  // InputManager.cpp), so overriding it here — before inputMgr.begin() reads
  // it — corrects this without touching the submodule. Revisit flipX/flipY
  // if tap positions are still off after this (this fixes the axis swap, not
  // necessarily left/right or up/down mirroring).
  BoardConfig::ACTIVE.touch.swapXY = false;

  // flipX=true was paired with swapXY=true upstream (it mirrors whichever
  // axis becomes "x" after the swap). With swap disabled, that same flag now
  // mirrors the wrong axis: on-hardware data showed leftmost tap -> high nx,
  // rightmost tap -> lower nx (inverted). Disabling it un-mirrors X.
  BoardConfig::ACTIVE.touch.flipX = false;

  // With the verified no-swap mounting, the GT911 reports its portrait
  // digitizer axes directly: X is 0..479 and Y is 0..799. The profile's
  // post-swap panel ranges (799 x 479) are correct only when swapXY is true;
  // leaving those ranges active compresses X and shifts later tabs left.
  BoardConfig::ACTIVE.touch.rawMinX = 0;
  BoardConfig::ACTIVE.touch.rawMaxX = 479;
  BoardConfig::ACTIVE.touch.rawMinY = 0;
  BoardConfig::ACTIVE.touch.rawMaxY = 799;

  inputMgr.begin();
  INX_SERIAL.printf("[STICKY] touch controller ready=%d ranges=x:%u..%u y:%u..%u\n", inputMgr.hasTouch() ? 1 : 0,
                 BoardConfig::ACTIVE.touch.rawMinX, BoardConfig::ACTIVE.touch.rawMaxX,
                 BoardConfig::ACTIVE.touch.rawMinY, BoardConfig::ACTIVE.touch.rawMaxY);
}

void HalGPIO::serviceTouchGestures() {
  touchPressedEvents = 0;
  touchReleasedEvents = 0;
  touchSwipeDirection = TouchSwipe::None;
  touchSwipeStartNx = 0.0f;
  touchSwipeStartNy = 0.0f;
  if (!inputMgr.hasTouch()) {
    return;
  }

  // No tap -> BTN_UP/BTN_DOWN synthesis here: a screen-half heuristic is right
  // for turning pages but wrong for grids/lists, where a tap should hit-test
  // against whatever's actually on screen at that position. Activities that
  // want tap-to-select use gpio.wasTouchTap()/hasTouch() directly instead of
  // relying on a synthesized button edge.

  // Horizontal swipe starting near a panel edge = back, matching the documented "swipe
  // from the screen edge toward the center to go back". Vertical swipes are classified first so a
  // top-down gesture can open the EPUB book-settings drawer without being consumed as Back.
  float sx = 0.0f, sy = 0.0f, ex = 0.0f, ey = 0.0f;
  if (inputMgr.wasSwipe(sx, sy, ex, ey)) {
    touchSwipeStartNx = sx;
    touchSwipeStartNy = sy;
    const float dx = ex - sx;
    const float dy = ey - sy;
    if (std::fabs(dy) > std::fabs(dx)) {
      // Keep the gesture logical here; MappedInputManager translates it into
      // the configured item-navigation button for the active menu layout.
      touchSwipeDirection = dy < 0.0f ? TouchSwipe::Up : TouchSwipe::Down;
      INX_SERIAL.printf("[STICKY][TOUCH] SWIPE %s\n", dy < 0.0f ? "up" : "down");
      return;
    }

    touchSwipeDirection = dx < 0.0f ? TouchSwipe::Left : TouchSwipe::Right;
    INX_SERIAL.printf("[STICKY][TOUCH] SWIPE %s\n", dx < 0.0f ? "left" : "right");

    const bool startedAtEdge = sx < kEdgeSwipeMargin || sx > (1.0f - kEdgeSwipeMargin) || sy < kEdgeSwipeMargin ||
                               sy > (1.0f - kEdgeSwipeMargin);
    if (startedAtEdge) {
      touchPressedEvents |= (1 << BTN_BACK);
      touchReleasedEvents |= (1 << BTN_BACK);
      return;
    }
  }
}

void HalGPIO::update() {
  inputMgr.update();
  float pressedNx = 0.0f;
  float pressedNy = 0.0f;
  if (inputMgr.wasTouchPressedAt(pressedNx, pressedNy)) {
    INX_SERIAL.printf("[STICKY][TOUCH] DOWN native=(%.3f,%.3f)\n", pressedNx, pressedNy);
  }
  if (inputMgr.wasTouchPressed()) {
    const InputManager::TouchPoint point = inputMgr.getTouchPoint();
    INX_SERIAL.printf("[STICKY][TOUCH] point=(%u,%u) valid=%d\n", point.x, point.y, point.valid ? 1 : 0);
  }
  if (inputMgr.wasTouchReleased()) {
    float tapNx = 0.0f;
    float tapNy = 0.0f;
    const bool isTap = inputMgr.wasTouchTap(tapNx, tapNy);
    INX_SERIAL.printf("[STICKY][TOUCH] UP tap=%d native=(%.3f,%.3f) held=%lu\n", isTap ? 1 : 0, tapNx, tapNy,
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
  const bool tapped = inputMgr.wasTouchTap(nx, ny);
  if (tapped) {
    // TEMPORARY diagnostic: every activity's tap-to-select code funnels
    // through here, so this confirms the raw signal is still reaching the
    // app regardless of which screen is active. Remove once "nothing opens"
    // is root-caused.
    INX_SERIAL.printf("[STICKY] wasTouchTap() -> nx=%.3f ny=%.3f\n", nx, ny);
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
  // No async/injected-press path wired up for Sticky yet (no BLE HID use case).
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

// Tilt-to-turn-page. Same gesture semantics/thresholds as lib/hal/HalGPIO.cpp's
// X3 implementation, adapted from raw QMI8658 gyro LSBs to Imu::Sample's
// already-converted degrees/second floats.
HalGPIO::MotionGesture HalGPIO::readMotionGesture(const uint8_t orientation, const uint8_t mode,
                                                   const uint8_t sensitivity) {
  freeink::Imu& imu = sharedImu();

  if (mode == 0 && !motionSensorInitialized) {
    return MotionGesture::None;
  }

  const unsigned long now = millis();
  if (motionLastPollMs != 0 && now - motionLastPollMs < 50) {
    return MotionGesture::None;
  }
  motionLastPollMs = now;

  if (!motionSensorInitialized) {
    if (!imu.begin()) {
      return MotionGesture::None;
    }
    motionSensorInitialized = true;
    motionSensorStartedMs = now;
    motionLastGestureMs = now;
  }

  if (mode == 0) {
    imu.sleep();
    motionSensorInitialized = false;
    motionGestureInProgress = false;
    return MotionGesture::None;
  }

  freeink::Imu::Sample sample{};
  if (!imu.read(sample)) {
    return MotionGesture::None;
  }

  if (now - motionSensorStartedMs < 300) {
    return MotionGesture::None;
  }

  float axis = 0.0f;
  switch (orientation) {
    case 1:  // Landscape clockwise
      axis = -sample.gy;
      break;
    case 2:  // Portrait inverted
      axis = -sample.gx;
      break;
    case 3:  // Landscape counter-clockwise
      axis = sample.gy;
      break;
    default:
      axis = sample.gx;
      break;
  }
  if (mode == 2) {
    axis = -axis;
  }

  constexpr float kNeutralThresholdDps = 50.0f;
  const float triggerDps = sensitivity >= 2 ? 180.0f : sensitivity == 0 ? 360.0f : 270.0f;

  if (motionGestureInProgress) {
    if (std::fabs(axis) < kNeutralThresholdDps) {
      motionGestureInProgress = false;
    }
    return MotionGesture::None;
  }

  if (now - motionLastGestureMs < 600) {
    return MotionGesture::None;
  }

  if (axis > triggerDps) {
    motionGestureInProgress = true;
    motionLastGestureMs = now;
    return MotionGesture::Next;
  }
  if (axis < -triggerDps) {
    motionGestureInProgress = true;
    motionLastGestureMs = now;
    return MotionGesture::Previous;
  }
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

bool HalGPIO::isUsbConnected() const {
  // Matches crosspoint-reader's real HalGPIO::isUsbConnected() for non-X3
  // devices: BoardConfig::ACTIVE.usbDetect is a plain digital pin, or
  // PIN_UNASSIGNED (-1) when the board has none — Sticky's usbDetect is
  // unassigned (its PWR_IN_VOLT is an ADC pin, not a digital detect), so this
  // is always false, same as upstream.
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
  // Fall back to the register only for an invalid RTC calendar date. On valid
  // dates this avoids stale weekday state left by earlier firmware.
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
  // Convert the application's 1=Monday..7=Sunday convention back to the
  // FreeInk RTC convention before writing it to hardware.
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

// Matches crosspoint-reader's real HalGPIO::getWakeupReason() (lib/hal/HalGPIO.cpp
// upstream). With isUsbConnected() correctly always false on Sticky (unassigned
// usbDetect), a fresh-flash POWERON reset naturally falls through to Other
// rather than being misclassified as AfterUSBPower — no device-specific
// carve-out needed here, unlike an earlier version of this function.
HalGPIO::WakeupReason HalGPIO::getWakeupReason() const {
  const auto wakeupCause = esp_sleep_get_wakeup_cause();
  const auto resetReason = esp_reset_reason();
  const bool usbConnected = isUsbConnected();

  if (resetReason == ESP_RST_DEEPSLEEP &&
      (wakeupCause == ESP_SLEEP_WAKEUP_GPIO || wakeupCause == ESP_SLEEP_WAKEUP_EXT1)) {
    return WakeupReason::PowerButton;
  }
  if (wakeupCause == ESP_SLEEP_WAKEUP_UNDEFINED && resetReason == ESP_RST_POWERON && !usbConnected) {
    return WakeupReason::PowerButton;
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
  static freeink::EnvironmentSensor sensor;
  if (!sensor.present() && !sensor.begin()) {
    return false;
  }
  return sensor.read(outTempC, outHumidityPct);
}
