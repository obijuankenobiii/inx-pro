#pragma once

#if FREEINK_DEVICE_X4PRO
#include <FrontlightManager.h>

#include "system/FrontlightPreferences.h"

class GfxRenderer;
class MappedInputManager;

extern FrontlightManager frontlight;

/**
 * Brightness UI helpers.
 *
 * The SDK maps 1% to a PWM duty of 1/1023 — the dimmest the hardware can produce — but a
 * LINEAR control never gets you there in practice: a 10% swipe step skips straight past it
 * to duty 12, and on the drawer slider the whole 1-10% range lives in the first few pixels
 * of the track. The light reads as "too bright even at minimum" purely because the bottom of
 * the range is unreachable, not because the hardware floor is high.
 *
 * Both helpers below weight the low end so the dim settings are actually selectable.
 */
namespace frontlight_ui {

/** One swipe step. Fine near the bottom, coarse where a 1% change is imperceptible. */
inline int stepBrightness(const int current, const bool up) {
  const int step = current < 10 ? 1 : (current < 30 ? 5 : 10);
  // Stepping down from 10 should land on 9, not 0 — pick the step for the value being
  // LEFT, not the one being arrived at.
  const int downStep = current <= 10 ? 1 : (current <= 30 ? 5 : 10);
  int next = current + (up ? step : -downStep);
  if (next < 0) next = 0;
  if (next > 100) next = 100;
  return next;
}

/** Slider position (0..1 along the track) -> brightness percent, square-law weighted so the
 *  left third of the track covers roughly 0-11%. */
inline int percentFromFraction(const float fraction) {
  const float f = fraction < 0.0f ? 0.0f : (fraction > 1.0f ? 1.0f : fraction);
  const int pct = static_cast<int>(100.0f * f * f + 0.5f);
  // Any deliberate touch above zero should light the panel, even at the far left.
  return (pct == 0 && f > 0.02f) ? 1 : pct;
}

/** Inverse of percentFromFraction(), for drawing the marker. */
inline float fractionFromPercent(const int percent) {
  const int p = percent < 0 ? 0 : (percent > 100 ? 100 : percent);
  float f = 0.0f;
  // sqrt without <cmath>: a few Newton steps are plenty for a marker position.
  if (p > 0) {
    f = static_cast<float>(p) / 100.0f;
    for (int i = 0; i < 12; ++i) f = 0.5f * (f + (static_cast<float>(p) / 100.0f) / f);
  }
  return f;
}


/** Write the live brightness/temperature back to NVS. */
inline void persist() {
  frontlight_preferences::Settings settings;
  frontlight_preferences::load(settings);
  if (frontlight.brightness() > 0) settings.brightness = frontlight.brightness();
  settings.enabled = frontlight.brightness() > 0 ? 1 : 0;
  settings.warmPercent = frontlight.colorTemperature();
  frontlight_preferences::save(settings);
}

/**
 * Edge-band frontlight gesture, shared by the reader and the Light drawer.
 *
 * A vertical swipe starting in the LEFT band adjusts colour temperature, in the RIGHT band
 * brightness. A swipe starting in the middle is left alone so the caller can use it for its
 * own thing - closing the book in the reader, closing the drawer on a page.
 *
 * Returns true when the gesture was consumed.
 */
inline bool handleEdgeSwipe(const MappedInputManager& input, const GfxRenderer& renderer) {
  constexpr float kBandLeftMaxNx = 0.22f;
  constexpr float kBandRightMinNx = 0.78f;
  if (!frontlight.present()) return false;

  float startNx = 0.0f;
  float startNy = 0.0f;
  bool up = false;
  if (input.wasTouchSwipeUpInScreen(renderer, startNx, startNy)) {
    up = true;
  } else if (!input.wasTouchSwipeDownInScreen(renderer, startNx, startNy)) {
    return false;
  }

  if (startNx <= kBandLeftMaxNx) {
    const int next = stepBrightness(frontlight.colorTemperature(), up);
    frontlight.setColorTemperature(static_cast<uint8_t>(next));
    persist();
    INX_SERIAL.printf("[LIGHT] temperature -> %d%%\n", next);
    return true;
  }
  if (startNx >= kBandRightMinNx) {
    const int next = stepBrightness(frontlight.brightness(), up);
    frontlight.setBrightness(static_cast<uint8_t>(next));
    persist();
    INX_SERIAL.printf("[LIGHT] brightness -> %d%%\n", next);
    return true;
  }
  return false;  // middle band: caller keeps it
}

}  // namespace frontlight_ui
#endif
