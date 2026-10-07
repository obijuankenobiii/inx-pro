#pragma once

/**
 * Murphy M4 FT6336U touch driver.
 *
 * This is intentionally kept with the Murphy HAL rather than in freeink-sdk:
 * the M4 uses a board-specific I2C bus/reset sequence and its INT signal is a
 * short report pulse that must not be used as a hard polling gate.
 */

#include <Arduino.h>

#include <cstdint>

class MurphyTouchDriver {
 public:
  struct TouchPoint {
    bool valid = false;
    uint16_t x = 0;
    uint16_t y = 0;
    unsigned long timestamp = 0;
  };

  void begin();
  void update();

  bool hasTouch() const { return enabled_; }
  bool isPressed() const { return pressed_; }
  bool wasPressedAt(float& nx, float& ny) const;
  bool isHeldAt(float& nx, float& ny) const;
  bool wasActivity() const { return pressedEvent_ || releasedEvent_; }
  bool wasTap(float& nx, float& ny) const;
  unsigned long lastHeldMs() const { return lastHeldMs_; }
  bool wasSwipe(float& nxStart, float& nyStart, float& nxEnd, float& nyEnd) const;

  TouchPoint point() const { return point_; }

  /** Suppress the current contact through its release edge. */
  void suppressCurrentContact();

 private:
  bool readFrame(uint8_t* frame, size_t length) const;
  void finishRelease(unsigned long now);
  void normalizePoint(uint16_t x, uint16_t y, float& nx, float& ny) const;
  uint16_t mapAxis(uint16_t raw, uint16_t rawMin, uint16_t rawMax, uint16_t outMax) const;

  bool enabled_ = false;
  bool pressed_ = false;
  bool pressedEvent_ = false;
  bool releasedEvent_ = false;
  bool suppressed_ = false;
  bool movedBeyondTapSlop_ = false;
  bool movedBeyondReleaseSlop_ = false;
  unsigned long nextReadAt_ = 0;
  unsigned long lastHeldMs_ = 0;
  TouchPoint point_;
  TouchPoint downPoint_;
  TouchPoint upPoint_;

  static constexpr unsigned long kSampleDelayMs = 8;
  static constexpr unsigned long kStaleReleaseMs = 100;
  static constexpr unsigned long kSwipeMaxMs = 700;
  static constexpr int kTapSlopPx = 28;
  static constexpr int kSwipeMinPx = 60;
};

