#include "MurphyTouchDriver.h"

#include <BoardConfig.h>
#include <driver/gpio.h>

#include <algorithm>
#include <cstdlib>

#include "MurphyM4I2c.h"

namespace {
bool writeRegister(const uint8_t reg, const uint8_t value) {
  const auto& touch = BoardConfig::ACTIVE.touch;
  const uint8_t payload[] = {reg, value};
  return murphy_m4_i2c::write(murphy_m4_i2c::touchDevice(touch.sda, touch.scl, touch.i2cAddress), payload,
                               sizeof(payload));
}
}  // namespace

void MurphyTouchDriver::begin() {
  const auto& touch = BoardConfig::ACTIVE.touch;

  if (touch.powerEnable >= 0) {
    gpio_hold_dis(static_cast<gpio_num_t>(touch.powerEnable));
    pinMode(touch.powerEnable, OUTPUT);
    digitalWrite(touch.powerEnable, touch.powerEnableActiveHigh ? HIGH : LOW);
    delay(500);
  }

  if (touch.reset >= 0) {
    pinMode(touch.reset, OUTPUT);
    digitalWrite(touch.reset, LOW);
    delay(50);
    digitalWrite(touch.reset, HIGH);
    delay(100);
  }
  if (touch.irq >= 0) pinMode(touch.irq, INPUT_PULLUP);

  if (touch.sda < 0 || touch.scl < 0 ||
      murphy_m4_i2c::touchDevice(touch.sda, touch.scl, touch.i2cAddress) == nullptr) {
    INX_SERIAL.println("[MURPHY_M4][TOUCH] I2C setup failed");
    return;
  }

  // These are the volatile settings used by the Murphy Reader controller.
  // The driver remains in polling mode; INT is diagnostic only because its
  // report pulses are shorter than the application's normal loop interval.
  const bool modeOk = writeRegister(0x00, 0x00);
  const bool thresholdOk = writeRegister(0x80, 0x16);
  const bool rateOk = writeRegister(0x88, 0x04);
  uint8_t probe[5] = {};
  const bool probeOk = readFrame(probe, sizeof(probe));
  enabled_ = modeOk && thresholdOk && rateOk && probeOk;
  INX_SERIAL.printf("[MURPHY_M4][TOUCH] FT6336U ready=%d mode=%d threshold=%d rate=%d\n", enabled_ ? 1 : 0,
                    modeOk ? 1 : 0, thresholdOk ? 1 : 0, rateOk ? 1 : 0);
}

void MurphyTouchDriver::update() {
  pressedEvent_ = false;
  releasedEvent_ = false;

  if (!pressed_ && !releasedEvent_) suppressed_ = false;
  if (!enabled_) return;

  const unsigned long now = millis();
  if (now < nextReadAt_) return;
  nextReadAt_ = now + kSampleDelayMs;

  // Register 0x02 returns TD_STATUS followed by the first point's XH/XL/YH/YL.
  uint8_t frame[11] = {};
  if (!readFrame(frame, sizeof(frame))) {
    if (pressed_ && now - point_.timestamp >= kStaleReleaseMs) finishRelease(now);
    return;
  }

  const uint8_t count = frame[0] & 0x0F;
  const uint8_t event = (frame[1] >> 6) & 0x03;
  if (count == 0 || event == 1) {
    if (pressed_) finishRelease(now);
    return;
  }

  const auto& touch = BoardConfig::ACTIVE.touch;
  const uint16_t rawX = (static_cast<uint16_t>(frame[1] & 0x0F) << 8) | frame[2];
  const uint16_t rawY = (static_cast<uint16_t>(frame[3] & 0x0F) << 8) | frame[4];
  const uint16_t panelX = touch.swapXY ? rawY : rawX;
  const uint16_t panelY = touch.swapXY ? rawX : rawY;
  if (panelX > static_cast<uint16_t>(touch.rawMaxX) || panelY > static_cast<uint16_t>(touch.rawMaxY)) return;

  point_.valid = true;
  point_.x = mapAxis(panelX, touch.rawMinX, touch.rawMaxX, touch.rawMaxX - touch.rawMinX);
  point_.y = mapAxis(panelY, touch.rawMinY, touch.rawMaxY, touch.rawMaxY - touch.rawMinY);
  if (touch.flipX) point_.x = static_cast<uint16_t>((touch.rawMaxX - touch.rawMinX) - point_.x);
  if (touch.flipY) point_.y = static_cast<uint16_t>((touch.rawMaxY - touch.rawMinY) - point_.y);
  point_.timestamp = now;

  if (!pressed_) {
    pressed_ = true;
    pressedEvent_ = true;
    downPoint_ = point_;
    upPoint_ = point_;
    movedBeyondTapSlop_ = false;
  } else {
    upPoint_ = point_;
  }

  const int dx = static_cast<int>(upPoint_.x) - static_cast<int>(downPoint_.x);
  const int dy = static_cast<int>(upPoint_.y) - static_cast<int>(downPoint_.y);
  if (std::abs(dx) > kTapSlopPx || std::abs(dy) > kTapSlopPx) movedBeyondTapSlop_ = true;
}

bool MurphyTouchDriver::readFrame(uint8_t* frame, const size_t length) const {
  const auto& touch = BoardConfig::ACTIVE.touch;
  const uint8_t reg = 0x02;
  return murphy_m4_i2c::read(murphy_m4_i2c::touchDevice(touch.sda, touch.scl, touch.i2cAddress), reg, frame, length);
}

void MurphyTouchDriver::finishRelease(const unsigned long now) {
  releasedEvent_ = true;
  lastHeldMs_ = now - downPoint_.timestamp;
  upPoint_ = point_;
  pressed_ = false;
  point_.valid = false;
}

void MurphyTouchDriver::normalizePoint(const uint16_t x, const uint16_t y, float& nx, float& ny) const {
  const auto& touch = BoardConfig::ACTIVE.touch;
  const float width = static_cast<float>(std::max<uint16_t>(1, touch.rawMaxX - touch.rawMinX));
  const float height = static_cast<float>(std::max<uint16_t>(1, touch.rawMaxY - touch.rawMinY));
  nx = std::clamp(static_cast<float>(x) / width, 0.0f, 1.0f);
  ny = std::clamp(static_cast<float>(y) / height, 0.0f, 1.0f);
}

uint16_t MurphyTouchDriver::mapAxis(const uint16_t raw, const uint16_t rawMin, const uint16_t rawMax,
                                    const uint16_t outMax) const {
  if (raw <= rawMin) return 0;
  if (raw >= rawMax) return outMax;
  return static_cast<uint32_t>(raw - rawMin) * outMax / (rawMax - rawMin);
}

bool MurphyTouchDriver::wasPressedAt(float& nx, float& ny) const {
  if (!pressedEvent_ || suppressed_) return false;
  normalizePoint(downPoint_.x, downPoint_.y, nx, ny);
  return true;
}

bool MurphyTouchDriver::isHeldAt(float& nx, float& ny) const {
  if (!pressed_ || suppressed_) return false;
  normalizePoint(point_.x, point_.y, nx, ny);
  return true;
}

bool MurphyTouchDriver::wasTap(float& nx, float& ny) const {
  if (!releasedEvent_ || suppressed_ || movedBeyondTapSlop_) return false;
  normalizePoint(downPoint_.x, downPoint_.y, nx, ny);
  return true;
}

bool MurphyTouchDriver::wasSwipe(float& nxStart, float& nyStart, float& nxEnd, float& nyEnd) const {
  if (!releasedEvent_ || suppressed_ || lastHeldMs_ > kSwipeMaxMs) return false;
  const int dx = static_cast<int>(upPoint_.x) - static_cast<int>(downPoint_.x);
  const int dy = static_cast<int>(upPoint_.y) - static_cast<int>(downPoint_.y);
  if (std::abs(dx) < kSwipeMinPx && std::abs(dy) < kSwipeMinPx) return false;
  normalizePoint(downPoint_.x, downPoint_.y, nxStart, nyStart);
  normalizePoint(upPoint_.x, upPoint_.y, nxEnd, nyEnd);
  return true;
}

void MurphyTouchDriver::suppressCurrentContact() {
  if (pressed_ || releasedEvent_) suppressed_ = true;
}

