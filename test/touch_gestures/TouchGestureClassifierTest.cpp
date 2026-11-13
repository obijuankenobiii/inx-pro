#include "system/TouchGestureClassifier.h"

#include <cassert>
#include <cstdio>
#include <initializer_list>

struct TouchInput {
  bool down = false, pressed = false, tap = false, swipe = false;
  float sx = 0.5f, sy = 0.5f, x = sx, y = sy;
  unsigned long duration = 150;

  bool wasTouchPressedAt(float& nx, float& ny) const {
    nx = sx;
    ny = sy;
    return down;
  }
  bool isTouchHeldAt(float& nx, float& ny) const {
    nx = x;
    ny = y;
    return pressed;
  }
  bool isTouchPressed() const { return pressed; }
  unsigned long lastTouchHeldMs() const { return duration; }
  bool wasTouchTap(float& nx, float& ny) const {
    nx = sx;
    ny = sy;
    return tap;
  }
  bool wasSwipe(float& nx, float& ny, float& ex, float& ey) const {
    nx = sx;
    ny = sy;
    ex = x;
    ey = y;
    return swipe;
  }
};

void gesture(int width, int height, float dx, float dy, bool expectTap, bool expectSwipe,
             bool returnToStart = false, bool backendEligible = true, unsigned long duration = 150) {
  TouchGestureClassifier classifier;
  TouchInput input;
  input.down = input.pressed = true;
  classifier.update(input, width, height);
  float sx, sy, ex, ey;
  assert(!classifier.wasTap(sx, sy));

  input.down = false;
  input.x += dx / width;
  input.y += dy / height;
  classifier.update(input, width, height);
  assert(!classifier.wasTap(sx, sy));
  if (returnToStart) {
    input.x = input.sx;
    input.y = input.sy;
    classifier.update(input, width, height);
  }

  input.pressed = false;
  input.duration = duration;
  // Simulate the backend's permissive tap policy below 60px.
  input.swipe = backendEligible && !returnToStart &&
                (std::fabs(dx) >= 60 || std::fabs(dy) >= 60) && duration <= 700;
  input.tap = backendEligible && (returnToStart || (std::fabs(dx) < 60 && std::fabs(dy) < 60));
  classifier.update(input, width, height);
  assert(classifier.wasTap(sx, sy) == expectTap);
  assert(classifier.wasSwipe(sx, sy, ex, ey) == expectSwipe);
  assert(!(classifier.wasTap(sx, sy) && classifier.wasSwipe(sx, sy, ex, ey)));
  if (expectSwipe) {
    assert(std::fabs((ex - sx) * width - dx) < 0.01f);
    assert(std::fabs((ey - sy) * height - dy) < 0.01f);
  }

  input.tap = input.swipe = false;
  classifier.update(input, width, height);
  assert(!classifier.wasTap(sx, sy));
  assert(!classifier.wasSwipe(sx, sy, ex, ey));

  // A new tap after a swipe must still work.
  input = {};
  input.down = input.pressed = true;
  classifier.update(input, width, height);
  input.down = input.pressed = false;
  input.tap = true;
  classifier.update(input, width, height);
  assert(classifier.wasTap(sx, sy));
}

int main() {
  for (const int width : {480, 800, 960}) {
    for (const int height : {480, 800, 1280}) {
      gesture(width, height, 0, 0, true, false);
      gesture(width, height, 18, -18, true, false);
      gesture(width, height, -19, 0, true, false);
      // Both native axes cover horizontal swipes in rotated UI orientations.
      for (const float distance : {-160.f, -59.f, -40.f, -28.f, -20.f, 20.f, 28.f, 40.f, 59.f, 160.f}) {
        gesture(width, height, distance, 0, false, true);
        gesture(width, height, 0, distance, false, true);
        gesture(width, height, distance, 0, false, false, true);
        gesture(width, height, distance, 0, false, false, false, false);
        gesture(width, height, distance, 0, false, false, false, true, 900);
      }
    }
  }
  // If press/move/release were sampled together, use the backend swipe endpoints.
  TouchGestureClassifier classifier;
  TouchInput input;
  input.x = 0.2f;
  input.swipe = true;
  classifier.update(input, 800, 480);
  float sx, sy, ex, ey;
  assert(classifier.wasSwipe(sx, sy, ex, ey));
  assert(!classifier.wasTap(sx, sy));
  std::puts("Touch gesture regression tests passed");
}
