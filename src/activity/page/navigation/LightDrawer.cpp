#include "LightDrawer.h"

#if FREEINK_DEVICE_X4PRO

#include <Arduino.h>
#include <GfxRenderer.h>

#include <algorithm>

#include "images/DarkMode.h"
#include "images/LibraryFilterLeft.h"
#include "images/LibraryFilterRight.h"
#include "images/LightOff.h"
#include "images/LightOn.h"
#include "system/Fonts.h"
#include "system/Frontlight.h"
#include "system/FrontlightPreferences.h"
#include "state/SystemSetting.h"

namespace navigation {
namespace {
constexpr int lightDrawerHeight = 330;
constexpr int lightButtonSize = 40;
constexpr int lightButtonY = 30;
constexpr int lightButtonRightMargin = 20;
constexpr int lightButtonGap = 10;
constexpr int lightCaretSize = 30;
constexpr int lightCaretGap = 8;
constexpr int lightCaretTouchWidth = 60;
constexpr int lightCaretTouchHeight = 72;
constexpr int lightTrackX = 56;
constexpr int lightTrackWidth = 368;
constexpr int lightKnobRadius = 12;
constexpr int lightKnobBorder = 2;
constexpr int lightBrightnessLabelY = 120;
constexpr int lightBrightnessY = 176;
constexpr int lightTemperatureLabelY = 220;
constexpr int lightTemperatureY = 266;

int lightPercentFromTap(const int tapX) {
  const int clamped = std::max(lightTrackX, std::min(lightTrackX + lightTrackWidth, tapX));
  const float fraction = static_cast<float>(clamped - lightTrackX) / static_cast<float>(lightTrackWidth);
  // Square-law, so the dim end of the range gets real travel on the track instead of a
  // couple of pixels — see frontlight_ui in system/Frontlight.h.
  return frontlight_ui::percentFromFraction(fraction);
}

void drawLightTrack(const GfxRenderer& renderer, const int y, const int value) {
  const int caretY = y - lightCaretSize / 2;
  renderer.bitmap.icon(LibraryFilterLeft, lightTrackX - lightCaretGap - lightCaretSize, caretY, lightCaretSize,
                       lightCaretSize);
  renderer.bitmap.icon(LibraryFilterRight, lightTrackX + lightTrackWidth + lightCaretGap, caretY, lightCaretSize,
                       lightCaretSize);
  constexpr int lightTrackHeight = 4;
  renderer.rectangle.fill(lightTrackX, y - lightTrackHeight / 2, lightTrackWidth, lightTrackHeight, true, true);
  // Marker uses the inverse curve so it sits under the finger that set it.
  const int markerX = lightTrackX + static_cast<int>(lightTrackWidth * frontlight_ui::fractionFromPercent(value));
  renderer.circle.render(markerX, y, lightKnobRadius, true);
  renderer.circle.render(markerX, y, lightKnobRadius - lightKnobBorder, false);
}

bool isInCaret(const int tapX, const int tapY, const int trackY, const bool right) {
  const int caretX = right ? lightTrackX + lightTrackWidth + lightCaretGap
                           : lightTrackX - lightCaretGap - lightCaretSize;
  const int touchX = caretX - (lightCaretTouchWidth - lightCaretSize) / 2;
  return tapX >= touchX && tapX < touchX + lightCaretTouchWidth &&
         tapY >= trackY - lightCaretTouchHeight / 2 && tapY < trackY + lightCaretTouchHeight / 2;
}

void saveLightPreferences() {
  frontlight_preferences::Settings settings;
  frontlight_preferences::load(settings);
  if (frontlight.brightness() > 0) {
    settings.brightness = frontlight.brightness();
  }
  settings.enabled = frontlight.brightness() > 0 ? 1 : 0;
  settings.warmPercent = frontlight.colorTemperature();
  frontlight_preferences::save(settings);
}
}  // namespace

LightDrawer::LightDrawer(GfxRenderer& renderer) : drawerRenderer(renderer) {}

void LightDrawer::render() const {
  if (!open || !frontlight.present()) return;

  const int width = drawerRenderer.getScreenWidth();
  // Scrim: 50% dither over the page below the drawer, so the drawer reads as a modal layer
  // rather than as content that happens to be drawn on top of the page.
  const int screenHeight = drawerRenderer.getScreenHeight();
  // if (screenHeight > lightDrawerHeight) {
  //   drawerRenderer.rectangle.fill(0, lightDrawerHeight, width, screenHeight - lightDrawerHeight,
  //                                 static_cast<int>(GfxRenderer::FillTone::Dither));
  // }

  drawerRenderer.rectangle.fill(0, 0, width, lightDrawerHeight,
                                static_cast<int>(GfxRenderer::FillTone::Paper));
  drawerRenderer.rectangle.render(0, 0, width, lightDrawerHeight, true);
  drawerRenderer.text.render(MONTSERRAT_16_FONT_ID, 20, 30, "Light", true, EpdFontFamily::BOLD);

  const bool on = frontlight.brightness() > 0;
  const int lightButtonX = width - lightButtonRightMargin - lightButtonSize;
  const int darkModeButtonX = lightButtonX - lightButtonGap - lightButtonSize;
  drawerRenderer.bitmap.icon(DarkMode, darkModeButtonX, lightButtonY, lightButtonSize, lightButtonSize,
                             drawerRenderer.isDarkMode() ? BitmapRender::Orientation::Rotate180
                                                         : BitmapRender::Orientation::None);
  drawerRenderer.bitmap.icon(on ? LightOn : LightOff, lightButtonX, lightButtonY, lightButtonSize,
                             lightButtonSize);

  drawerRenderer.text.render(MONTSERRAT_10_FONT_ID, 20, lightBrightnessLabelY, "Brightness", true);
  drawLightTrack(drawerRenderer, lightBrightnessY, frontlight.brightness());
  drawerRenderer.text.render(MONTSERRAT_10_FONT_ID, 20, lightTemperatureLabelY, "Cool", true);
  const int warmWidth = drawerRenderer.text.getWidth(MONTSERRAT_10_FONT_ID, "Warm");
  drawerRenderer.text.render(MONTSERRAT_10_FONT_ID, width - 40 - warmWidth, lightTemperatureLabelY, "Warm",
                             true);
  drawLightTrack(drawerRenderer, lightTemperatureY, frontlight.colorTemperature());
}

// A down-swipe only opens the drawer if it STARTS within this fraction of the screen
// height from the top. Confirmed against hardware: a top-edge swipe reports
// native=(0.008, ...) and the renderer maps screen ny = nativeNx, so the top really is
// ny ~ 0. Mid-screen swipes fall through to the page underneath (library pagination).
constexpr float kOpenFromTopMaxNy = 0.15f;

LightDrawer::Action LightDrawer::handleInput(MappedInputManager& input) const {
  // While the drawer is open the edge bands adjust the light, exactly as they do in the
  // reader: left = colour temperature, right = brightness. Checked before the close gesture
  // so an edge swipe adjusts instead of dismissing; a middle swipe still closes.
  if (open && frontlight_ui::handleEdgeSwipe(input, drawerRenderer)) {
    return Action::Adjusted;
  }
  if (open && input.wasTouchSwipeUpForRenderer(drawerRenderer)) {
    open = false;
    return Action::Closed;
  }
  if (!open) {
    // Goal: only open from the top edge, so a mid-screen down-swipe stays available to the
    // page underneath (library pagination). The band is not gated yet — the swipe start is
    // reported in the renderer's logical frame, and after the X4 Pro's 90-degree touch
    // rotation it is not obvious which end of that axis is the physical top. Log the real
    // value first, then set the threshold from it rather than guessing.
    float startNx = 0.0f;
    float startNy = 0.0f;
    if (input.wasTouchSwipeDownInScreen(drawerRenderer, startNx, startNy)) {
      const bool fromTop = startNy <= kOpenFromTopMaxNy;
      INX_SERIAL.printf("[LIGHTDRAWER] swipe-down start nx=%.3f ny=%.3f fromTop=%d\n", startNx, startNy, fromTop);
      if (fromTop) {
        open = true;
        return Action::Opened;
      }
    } else if (input.wasTouchSwipeDownForRenderer(drawerRenderer)) {
      // Direction matched but no start position — open anyway rather than be unopenable.
      INX_SERIAL.printf("[LIGHTDRAWER] swipe-down with NO start position available\n");
      open = true;
      return Action::Opened;
    }
  }
  return Action::None;
}

LightDrawer::Action LightDrawer::handleTap(const int tapX, const int tapY) const {
  const int lightButtonX = drawerRenderer.getScreenWidth() - lightButtonRightMargin - lightButtonSize;
  const int darkModeButtonX = lightButtonX - lightButtonGap - lightButtonSize;
  if (tapX < 0 || tapX >= drawerRenderer.getScreenWidth() || tapY < 0 || tapY >= lightDrawerHeight) {
    open = false;
    return Action::Closed;
  }

  if (tapX >= darkModeButtonX && tapX < darkModeButtonX + lightButtonSize && tapY >= lightButtonY &&
      tapY < lightButtonY + lightButtonSize) {
    drawerRenderer.setDarkMode(!drawerRenderer.isDarkMode());
    SETTINGS.darkMode = drawerRenderer.isDarkMode() ? 1 : 0;
    SETTINGS.saveToFile();
    return Action::Opened;
  }

  if (tapX >= lightButtonX && tapX < lightButtonX + lightButtonSize && tapY >= lightButtonY &&
      tapY < lightButtonY + lightButtonSize) {
    if (frontlight.brightness() > 0)
      frontlight.off();
    else
      frontlight.on();
    saveLightPreferences();
    return Action::Opened;
  }

  if (isInCaret(tapX, tapY, lightBrightnessY, false) || isInCaret(tapX, tapY, lightBrightnessY, true)) {
    const bool increase = isInCaret(tapX, tapY, lightBrightnessY, true);
    const int next = frontlight_ui::stepBrightness(frontlight.brightness(), increase);
    frontlight.setBrightness(static_cast<uint8_t>(next));
    saveLightPreferences();
    return Action::Opened;
  }

  if (isInCaret(tapX, tapY, lightTemperatureY, false) || isInCaret(tapX, tapY, lightTemperatureY, true)) {
    const bool increase = isInCaret(tapX, tapY, lightTemperatureY, true);
    const int next = frontlight_ui::stepBrightness(frontlight.colorTemperature(), increase);
    frontlight.setColorTemperature(static_cast<uint8_t>(next));
    saveLightPreferences();
    return Action::Opened;
  }

  if (tapY >= lightBrightnessY - 24 && tapY <= lightBrightnessY + 24) {
    frontlight.setBrightness(static_cast<uint8_t>(lightPercentFromTap(tapX)));
    saveLightPreferences();
    return Action::Opened;
  }

  if (tapY >= lightTemperatureY - 24 && tapY <= lightTemperatureY + 24) {
    // Linear: colour temperature has no perceptual crowding at either end.
    const int clampedX = std::max(lightTrackX, std::min(lightTrackX + lightTrackWidth, tapX));
    frontlight.setColorTemperature(
        static_cast<uint8_t>((clampedX - lightTrackX) * 100 / lightTrackWidth));
    saveLightPreferences();
    return Action::Opened;
  }

  return Action::Opened;
}

}  // namespace navigation

#endif
