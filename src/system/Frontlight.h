#pragma once

#if INX_HAS_FRONTLIGHT
#include <FrontlightManager.h>

#include <algorithm>

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
 *
 * That holds for a 10-bit PWM. The T5 S3 drives its backlight at 8 bits, where 1-4% all
 * round to duty 1/255, which does not light the LED. INX_FRONTLIGHT_MIN_PERCENT sets the
 * dimmest percent that does, and the brightness helpers skip from 0 (off) straight to it.
 */
#ifndef INX_FRONTLIGHT_MIN_PERCENT
#define INX_FRONTLIGHT_MIN_PERCENT 1
#endif

namespace frontlight_ui {

constexpr int kMinLitPercent = INX_FRONTLIGHT_MIN_PERCENT;

/** One step of a 0-100 control. Fine near the bottom, coarse where 1% is imperceptible. */
inline int stepPercent(const int current, const bool up) {
  const int step = current < 10 ? 1 : (current < 30 ? 5 : 10);
  const int downStep = current <= 10 ? 1 : (current <= 30 ? 5 : 10);
  int next = current + (up ? step : -downStep);
  if (next < 0) next = 0;
  if (next > 100) next = 100;
  return next;
}

/** One brightness step: stepPercent(), but never into the unlit range below kMinLitPercent. */
inline int stepBrightness(const int current, const bool up) {
  const int next = stepPercent(current, up);
  if (next <= 0 || next >= kMinLitPercent) return next;
  return up ? kMinLitPercent : 0;
}

/** Slider position (0..1 along the track) -> brightness percent, square-law weighted so the
 *  left third of the track covers roughly the bottom 11% of the lit range. */
inline int percentFromFraction(const float fraction) {
  const float f = fraction < 0.0f ? 0.0f : (fraction > 1.0f ? 1.0f : fraction);
#if INX_FRONTLIGHT_MIN_PERCENT > 1
  if (f <= 0.02f) return 0;
  return kMinLitPercent + static_cast<int>(static_cast<float>(100 - kMinLitPercent) * f * f + 0.5f);
#else
  const int pct = static_cast<int>(100.0f * f * f + 0.5f);
  return (pct == 0 && f > 0.02f) ? 1 : pct;
#endif
}

/** Inverse of percentFromFraction(), for drawing the marker. */
inline float fractionFromPercent(const int percent) {
  const int p = percent < 0 ? 0 : (percent > 100 ? 100 : percent);
  if (p <= 0) return 0.0f;
#if INX_FRONTLIGHT_MIN_PERCENT > 1
  const float target =
      static_cast<float>(std::max(0, p - kMinLitPercent)) / static_cast<float>(100 - kMinLitPercent);
  if (target <= 0.0f) return 0.03f;  // dimmest lit setting: just off the "off" end
#else
  const float target = static_cast<float>(p) / 100.0f;
#endif
  float f = target;
  for (int i = 0; i < 12; ++i) f = 0.5f * (f + target / f);
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
 * A vertical swipe starting in the LEFT band adjusts colour temperature (boards with a warm/cool
 * pair only), in the RIGHT band brightness. A swipe starting in the middle is left alone so the caller can use it for its
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

  if (startNx <= kBandLeftMaxNx && frontlight.hasColorTemperature()) {
    const int next = stepPercent(frontlight.colorTemperature(), up);
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
  return false;
}

}
#endif
