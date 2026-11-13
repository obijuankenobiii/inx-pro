#pragma once

#include <cmath>

// App gesture policy, independent of the SDK's more permissive release-tap
// threshold. Coordinates stay in the native panel frame until UI mapping.
class TouchGestureClassifier {
 public:
  static constexpr int swipeMinPx = 20;
  // A contact that reaches the swipe threshold must never remain a tap.
  static constexpr int tapSlopPx = swipeMinPx - 1;
  static constexpr unsigned long swipeMaxMs = 700;

  template <typename Input>
  void update(const Input& input, int width, int height) {
    tap_ = false;
    swipe_ = false;
    float x = 0, y = 0;
    if (input.wasTouchPressedAt(x, y)) {
      active_ = true;
      moved_ = false;
      startX_ = endX_ = x;
      startY_ = endY_ = y;
    }
    if (active_ && input.isTouchHeldAt(x, y)) sample(x, y, width, height);
    if (input.isTouchPressed()) return;

    float sx = 0, sy = 0, ex = 0, ey = 0;
    const bool backendSwipe = input.wasSwipe(sx, sy, ex, ey);
    const bool backendTap = input.wasTouchTap(x, y);
    // Backend eligibility preserves contact suppression and multi-touch rules.
    if (backendSwipe) {
      if (!active_) {
        startX_ = sx;
        startY_ = sy;
        moved_ = false;
      }
      sample(ex, ey, width, height);
    }
    if (active_ || backendSwipe) {
      const long dx = std::lround(std::fabs(endX_ - startX_) * width);
      const long dy = std::lround(std::fabs(endY_ - startY_) * height);
      swipe_ = (backendTap || backendSwipe) && input.lastTouchHeldMs() <= swipeMaxMs &&
               (dx >= swipeMinPx || dy >= swipeMinPx);
      tap_ = backendTap && !moved_ && !swipe_;
    }
    active_ = false;
  }

  bool wasTap(float& x, float& y) const {
    if (!tap_) return false;
    x = startX_;
    y = startY_;
    return true;
  }

  bool wasSwipe(float& sx, float& sy, float& ex, float& ey) const {
    if (!swipe_) return false;
    sx = startX_;
    sy = startY_;
    ex = endX_;
    ey = endY_;
    return true;
  }

 private:
  void sample(float x, float y, int width, int height) {
    endX_ = x;
    endY_ = y;
    if (std::lround(std::fabs(x - startX_) * width) > tapSlopPx ||
        std::lround(std::fabs(y - startY_) * height) > tapSlopPx) moved_ = true;
  }

  bool active_ = false;
  bool moved_ = false;
  bool tap_ = false;
  bool swipe_ = false;
  float startX_ = 0, startY_ = 0, endX_ = 0, endY_ = 0;
};
