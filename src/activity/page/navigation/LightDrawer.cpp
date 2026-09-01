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
constexpr int lightKnobRadius = 15;
constexpr int lightKnobBorder = 2;
constexpr int lightBrightnessLabelY = 120;
constexpr int lightBrightnessY = 176;
constexpr int lightTemperatureLabelY = 220;
constexpr int lightTemperatureY = 266;

int lightValueFromTrack(const int tapX, const bool brightness) {
  const int clamped = std::max(lightTrackX, std::min(lightTrackX + lightTrackWidth, tapX));
  const float fraction = static_cast<float>(clamped - lightTrackX) / static_cast<float>(lightTrackWidth);
  return brightness ? frontlight_ui::percentFromFraction(fraction)
                    : static_cast<int>(fraction * 100.0f + 0.5f);
}

void drawLightCarets(const GfxRenderer& renderer, const int y) {
  const int caretY = y - lightCaretSize / 2;
  renderer.bitmap.icon(LibraryFilterLeft, lightTrackX - lightCaretGap - lightCaretSize, caretY, lightCaretSize,
                       lightCaretSize);
  renderer.bitmap.icon(LibraryFilterRight, lightTrackX + lightTrackWidth + lightCaretGap, caretY, lightCaretSize,
                       lightCaretSize);
}

void drawLightTrackBody(const GfxRenderer& renderer, const int y, const int value) {
  const int markerX = lightTrackX + static_cast<int>(lightTrackWidth * frontlight_ui::fractionFromPercent(value));
  constexpr int activeTrackHeight = 8;
  constexpr int inactiveTrackHeight = 4;
  if (markerX > lightTrackX) {
    renderer.rectangle.fill(lightTrackX, y - activeTrackHeight / 2, markerX - lightTrackX, activeTrackHeight,
                            static_cast<int>(GfxRenderer::FillTone::Ink), true);
  }
  if (markerX < lightTrackX + lightTrackWidth) {
    renderer.rectangle.fill(markerX, y - inactiveTrackHeight / 2,
                            lightTrackX + lightTrackWidth - markerX, inactiveTrackHeight,
                            static_cast<int>(GfxRenderer::FillTone::Gray), true);
  }
  renderer.circle.render(markerX, y, lightKnobRadius, true);
  renderer.circle.render(markerX, y, lightKnobRadius - lightKnobBorder, false);
}

void drawLightTrack(const GfxRenderer& renderer, const int y, const int value) {
  drawLightCarets(renderer, y);
  drawLightTrackBody(renderer, y, value);
}

/**
 * Band holding both carets, the whole track, and the knob at any position along it. It is
 * blanked before the drag's frame is stored, so the stored frame carries every pixel of
 * the screen except the throbber.
 */
constexpr int lightSliderStripX = lightTrackX - lightCaretGap - lightCaretSize;
constexpr int lightSliderStripWidth = lightTrackWidth + 2 * (lightCaretGap + lightCaretSize);
constexpr int lightSliderStripHalfHeight = lightKnobRadius + 2;
constexpr int lightSliderStripHeight = lightSliderStripHalfHeight * 2;

void clearLightSliderRow(const GfxRenderer& renderer, const int trackY) {
  renderer.rectangle.fill(lightSliderStripX, trackY - lightSliderStripHalfHeight, lightSliderStripWidth,
                          lightSliderStripHeight, static_cast<int>(GfxRenderer::FillTone::Paper));
  drawLightCarets(renderer, trackY);
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
}

LightDrawer::LightDrawer(GfxRenderer& renderer) : drawerRenderer(renderer) {}

void LightDrawer::refreshAfterSliderChange() const {
  drawerRenderer.syncWriteBufferFromActive();
  render();
  drawerRenderer.displayBufferAsync(HalDisplay::FAST_REFRESH);
  drawerRenderer.syncWriteBufferFromActive();
}

/**
 * Stores the whole frame for the duration of a slider drag.
 *
 * The dragged row is blanked back to paper and carets first, so what gets stored is the
 * entire screen minus the throbber: drawer chrome, both labels, the other slider, and the
 * page underneath. Each drag step then restores that frame and stamps the knob on top,
 * which is what keeps a live drag off the full render path.
 */
void LightDrawer::storeSliderFrame() const {
  sliderStripTrackY = sliderControl == 0 ? lightBrightnessY : lightTemperatureY;
  // Order matters. This panel is dual-buffer, so after the drawer's own refresh the write
  // buffer holds a stale frame and syncWriteBufferFromActive() has to run FIRST to pull
  // the displayed frame in. Running it after the blank would copy the on-screen knob right
  // back over the cleared row and store it.
  drawerRenderer.syncWriteBufferFromActive();
  clearLightSliderRow(drawerRenderer, sliderStripTrackY);
  sliderFrameStored = drawerRenderer.storeBwBuffer();
}

void LightDrawer::releaseSliderFrame() const {
  if (!sliderFrameStored) return;
  drawerRenderer.freeBwBufferChunks();
  sliderFrameStored = false;
}

/**
 * Restores the stored frame over the old throbber, draws the knob at the new value, and
 * pushes it. displayBufferAsync() drains any still-running refresh before starting the
 * next one, so this self-throttles to the panel's refresh rate and always shows the newest
 * knob position rather than queueing intermediate ones.
 */
void LightDrawer::renderSliderRowLive() const {
  if (!open || !frontlight.present()) return;

  // A FAST refresh on this panel is a ~460 ms full-screen DRF, and displayBufferAsync()
  // drains a pending one before starting the next. Calling it every time the value moves
  // therefore parks the whole input loop in that drain, so the frontlight itself only
  // tracks the finger twice a second. Skip instead: the drag keeps sampling touch and
  // driving the light live, and the knob repaints from the newest value once the panel
  // frees up.
  if (drawerRenderer.isRefreshBusy()) {
    sliderRepaintPending = true;
    return;
  }
  sliderRepaintPending = false;

  const bool brightness = sliderControl == 0;
  const int trackY = brightness ? lightBrightnessY : lightTemperatureY;
  const int value = brightness ? frontlight.brightness() : frontlight.colorTemperature();

  if (!sliderFrameStored || sliderStripTrackY != trackY || !drawerRenderer.copyStoredBwToFramebuffer()) {
    // No stored frame (allocation failed): pull the displayed frame in instead, or the
    // dual-buffer swap would leave the knob being drawn onto a stale frame.
    drawerRenderer.syncWriteBufferFromActive();
  }

  // Rebuild the row rather than trusting whatever the restore brought back: paper over any
  // knob still in the frame, carets and both track bars behind where it sat, then the knob
  // at its new position. Cheap next to the refresh, and it cannot leave a stuck throbber.
  clearLightSliderRow(drawerRenderer, trackY);
  drawLightTrackBody(drawerRenderer, trackY, value);

  drawerRenderer.displayBufferAsync(HalDisplay::FAST_REFRESH);
}

void LightDrawer::render() const {
  if (!open || !frontlight.present()) return;

  const int width = drawerRenderer.getScreenWidth();
  const int screenHeight = drawerRenderer.getScreenHeight();

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

constexpr float kOpenFromTopMaxNy = 0.15f;

LightDrawer::Action LightDrawer::handleInput(MappedInputManager& input) const {
  if (open && sliderDragging) {
    if (input.isTouchPressed()) {
      float nx = 0.0f;
      float ny = 0.0f;
      if (input.isTouchHeldInScreen(drawerRenderer, nx, ny)) {
        const int value = lightValueFromTrack(static_cast<int>(nx * drawerRenderer.getScreenWidth()),
                                              sliderControl == 0);
        const int current = sliderControl == 0 ? frontlight.brightness() : frontlight.colorTemperature();
        if (value != current) {
          if (sliderControl == 0) {
            frontlight.setBrightness(static_cast<uint8_t>(value));
          } else {
            frontlight.setColorTemperature(static_cast<uint8_t>(value));
          }
          sliderDragChanged = true;
          sliderRepaintPending = true;
        }
      }
      // Retry every pass, not just on a change: a move made while the panel was busy still
      // owes a repaint once it goes idle.
      if (sliderRepaintPending) renderSliderRowLive();
      return Action::Adjusted;
    }

    sliderDragging = false;
    sliderRepaintPending = false;
    releaseSliderFrame();
    if (sliderDragChanged) {
      saveLightPreferences();
      refreshAfterSliderChange();
    }
    sliderDragChanged = false;
    return Action::Adjusted;
  }

  if (open) {
    float nx = 0.0f;
    float ny = 0.0f;
    if (input.wasTouchPressedInScreen(drawerRenderer, nx, ny)) {
      const int x = static_cast<int>(nx * drawerRenderer.getScreenWidth());
      const int y = static_cast<int>(ny * drawerRenderer.getScreenHeight());
      const bool inBrightnessTrack = y >= lightBrightnessY - 24 && y <= lightBrightnessY + 24 &&
                                     x >= lightTrackX - lightKnobRadius &&
                                     x <= lightTrackX + lightTrackWidth + lightKnobRadius;
      const bool inTemperatureTrack = y >= lightTemperatureY - 24 && y <= lightTemperatureY + 24 &&
                                      x >= lightTrackX - lightKnobRadius &&
                                      x <= lightTrackX + lightTrackWidth + lightKnobRadius;
      if (inBrightnessTrack || inTemperatureTrack) {
        sliderControl = inBrightnessTrack ? 0 : 1;
        sliderDragging = true;
        sliderDragChanged = false;
        storeSliderFrame();
        const int value = lightValueFromTrack(x, sliderControl == 0);
        const int current = sliderControl == 0 ? frontlight.brightness() : frontlight.colorTemperature();
        if (value != current) {
          if (sliderControl == 0) {
            frontlight.setBrightness(static_cast<uint8_t>(value));
          } else {
            frontlight.setColorTemperature(static_cast<uint8_t>(value));
          }
          sliderDragChanged = true;
        }
        // Always repaint: storing the frame cleared the row, so the knob has to be put
        // back even when the press landed on the value the slider already held.
        renderSliderRowLive();
        return Action::Adjusted;
      }
    }
  }

  if (open && frontlight_ui::handleEdgeSwipe(input, drawerRenderer)) {
    return Action::Adjusted;
  }
  if (open && input.wasTouchSwipeUpForRenderer(drawerRenderer)) {
    open = false;
    return Action::Closed;
  }
  if (!open) {
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
    frontlight.setBrightness(static_cast<uint8_t>(lightValueFromTrack(tapX, true)));
    saveLightPreferences();
    return Action::Opened;
  }

  if (tapY >= lightTemperatureY - 24 && tapY <= lightTemperatureY + 24) {
    frontlight.setColorTemperature(static_cast<uint8_t>(lightValueFromTrack(tapX, false)));
    saveLightPreferences();
    return Action::Opened;
  }

  return Action::Opened;
}

}

#endif
