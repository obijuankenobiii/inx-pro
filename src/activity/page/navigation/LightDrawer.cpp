#include "LightDrawer.h"

#if INX_HAS_FRONTLIGHT || FREEINK_DEVICE_X4PRO || FREEINK_DEVICE_MURPHY_M4 || FREEINK_DEVICE_PICCO

#include <Arduino.h>
#include <GfxRenderer.h>

#include <algorithm>

#include "Menu.h"
#include "images/LibraryFilterLeft.h"
#include "images/LibraryFilterRight.h"
#include "images/LightOff.h"
#include "images/LightOn.h"
#include "images/Moon.h"
#include "images/Sun.h"
#include "system/Fonts.h"
#include "system/Frontlight.h"
#include "system/FrontlightPreferences.h"
#include "system/ScreenComponents.h"
#include "state/SystemSetting.h"

namespace navigation {
namespace {
constexpr int lightButtonSize = Menu::iconSize;
constexpr int lightButtonY = Menu::topPadding;
constexpr int lightButtonRightMargin = Menu::leftMargin;
constexpr int lightButtonGap = 10;
constexpr int lightCaretSize = 30;
constexpr int lightCaretGap = 8;
constexpr int lightCaretTouchWidth = 60;
constexpr int lightCaretTouchHeight = 72;
constexpr int lightTrackX = 56;
constexpr int lightTrackWidth = 368;
constexpr int lightKnobRadius = 15;
constexpr int lightKnobBorder = 2;
constexpr int lightBrightnessLabelY = 110;
constexpr int lightBrightnessY = 176;
constexpr int lightTemperatureLabelY = 230;
constexpr int lightTemperatureY = 296;
constexpr int lightIndicatorWidth = 96;
constexpr int lightIndicatorHeight = 6;
constexpr int lightIndicatorBottomMargin = 20;



bool hasTemperatureSlider() { return frontlight.hasColorTemperature(); }
int lightDrawerHeight() { return hasTemperatureSlider() ? 380 : 240; }

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

void drawLightDrawerIndicator(const GfxRenderer& renderer, const int width) {
  const int x = (width - lightIndicatorWidth) / 2;
  const int y = lightDrawerHeight() - lightIndicatorBottomMargin - lightIndicatorHeight;
  const int radius = lightIndicatorHeight / 2;
  const int middleWidth = lightIndicatorWidth - 2 * radius;
  renderer.rectangle.fill(x + radius, y, middleWidth, lightIndicatorHeight,
                          static_cast<int>(GfxRenderer::FillTone::Ink), true);
  renderer.circle.render(x + radius, y + radius, radius, true);
  renderer.circle.render(x + lightIndicatorWidth - radius - 1, y + radius, radius, true);
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

LightDrawer& globalLightDrawer(GfxRenderer& renderer) {
  static LightDrawer drawer(renderer);
  return drawer;
}

LightDrawer::LightDrawer(GfxRenderer& renderer) : drawerRenderer(renderer) {}

void LightDrawer::refreshAfterSliderChange() const {
  drawerRenderer.syncWriteBufferFromActive();
  render();
  drawerRenderer.displayBufferAsync(HalDisplay::FAST_REFRESH);
  drawerRenderer.syncWriteBufferFromActive();
}

/**
 * Stores the page frame for the duration of the drawer.
 */
void LightDrawer::storeDrawerFrame() const {


  drawerRenderer.syncWriteBufferFromActive();
  drawerRenderer.freeBwBufferChunks();
  drawerFrameStored = drawerRenderer.storeBwBuffer();
  frameRestorePending = false;
}

void LightDrawer::releaseDrawerFrame() const {
  if (!drawerFrameStored) return;
  drawerRenderer.freeBwBufferChunks();
  drawerFrameStored = false;
}

bool LightDrawer::restoreStoredFrameForRender() const {
  if (!drawerFrameStored || (!open && !frameRestorePending)) return false;
  if (!drawerRenderer.copyStoredBwToFramebuffer()) {
    releaseDrawerFrame();
    frameRestorePending = false;
    return false;
  }
  if (!open) {
    frameRestorePending = false;
    releaseDrawerFrame();
  }
  return true;
}

/**
 * Restores the stored frame over the old throbber, draws the knob at the new value, and
 * pushes it. displayBufferAsync() drains any still-running refresh before starting the
 * next one, so this self-throttles to the panel's refresh rate and always shows the newest
 * knob position rather than queueing intermediate ones.
 */
void LightDrawer::renderSliderRowLive() const {
  if (!open || !frontlight.present()) return;







  if (drawerRenderer.isRefreshBusy()) {
    sliderRepaintPending = true;
    return;
  }
  sliderRepaintPending = false;

  const bool brightness = sliderControl == 0;
  const int trackY = brightness ? lightBrightnessY : lightTemperatureY;
  const int value = brightness ? frontlight.brightness() : frontlight.colorTemperature();



  drawerRenderer.syncWriteBufferFromActive();




  clearLightSliderRow(drawerRenderer, trackY);
  drawLightTrackBody(drawerRenderer, trackY, value);

  drawerRenderer.displayBufferAsync(HalDisplay::FAST_REFRESH);
}

void LightDrawer::render() const {
  if (!open || !frontlight.present()) return;

  const int width = drawerRenderer.getScreenWidth();
  ScreenComponents::drawWhiteOverlay(drawerRenderer, lightDrawerHeight());
  drawerRenderer.rectangle.fill(0, 0, width, lightDrawerHeight(),
                                static_cast<int>(GfxRenderer::FillTone::Paper));
  drawerRenderer.rectangle.render(0, 0, width, lightDrawerHeight(), true);
  const int titleFont = systemFontIdForPointSize(16);
  const int titleY = Menu::topPadding + (Menu::iconSize - drawerRenderer.text.getLineHeight(titleFont)) / 2;
  drawerRenderer.text.render(titleFont, Menu::leftMargin, titleY, "Light", true, EpdFontFamily::BOLD);

  const bool on = frontlight.brightness() > 0;
  const int lightButtonX = width - lightButtonRightMargin - lightButtonSize;
  const int darkModeButtonX = lightButtonX - lightButtonGap - lightButtonSize;
  const uint8_t* modeIcon = drawerRenderer.isDarkMode() ? Sun : Moon;
  drawerRenderer.bitmap.icon(modeIcon, darkModeButtonX, lightButtonY, lightButtonSize, lightButtonSize);
  drawerRenderer.bitmap.icon(on ? LightOn : LightOff, lightButtonX, lightButtonY, lightButtonSize,
                             lightButtonSize);

  const int labelFont = systemFontId();
  drawerRenderer.text.render(labelFont, 20, lightBrightnessLabelY, "Brightness", true);
  drawLightTrack(drawerRenderer, lightBrightnessY, frontlight.brightness());
  if (!hasTemperatureSlider()) return;
  drawerRenderer.text.render(labelFont, 20, lightTemperatureLabelY, "Cool", true);
  const int warmWidth = drawerRenderer.text.getWidth(labelFont, "Warm");
  drawerRenderer.text.render(labelFont, width - 40 - warmWidth, lightTemperatureLabelY, "Warm", true);
  drawLightTrack(drawerRenderer, lightTemperatureY, frontlight.colorTemperature());
  drawLightDrawerIndicator(drawerRenderer, width);
}

constexpr float kOpenFromTopMaxNy = 0.15f;

LightDrawer::Action LightDrawer::handleInput(MappedInputManager& input, const bool allowOpen) const {
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




#if !FREEINK_DEVICE_PICCO
      if (sliderRepaintPending) renderSliderRowLive();
#endif
      return Action::Adjusted;
    }

    sliderDragging = false;
    sliderRepaintPending = false;
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



      const bool onCaret = isInCaret(x, y, lightBrightnessY, false) || isInCaret(x, y, lightBrightnessY, true) ||
                           isInCaret(x, y, lightTemperatureY, false) || isInCaret(x, y, lightTemperatureY, true);
      const bool inBrightnessTrack = !onCaret && y >= lightBrightnessY - 24 && y <= lightBrightnessY + 24 &&
                                     x >= lightTrackX - lightKnobRadius &&
                                     x <= lightTrackX + lightTrackWidth + lightKnobRadius;
      const bool inTemperatureTrack = !onCaret && hasTemperatureSlider() && y >= lightTemperatureY - 24 &&
                                      y <= lightTemperatureY + 24 && x >= lightTrackX - lightKnobRadius &&
                                      x <= lightTrackX + lightTrackWidth + lightKnobRadius;
      if (inBrightnessTrack || inTemperatureTrack) {
        sliderControl = inBrightnessTrack ? 0 : 1;
        sliderDragging = true;
        sliderDragChanged = false;


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


#if !FREEINK_DEVICE_PICCO
        renderSliderRowLive();
#endif
        return Action::Adjusted;
      }
    }
  }

  if (open && frontlight_ui::handleEdgeSwipe(input, drawerRenderer)) {
    return Action::Adjusted;
  }
  if (open && input.wasTouchSwipeUpForRenderer(drawerRenderer)) {
    open = false;
    frameRestorePending = true;
    return Action::Closed;
  }
  if (!open && allowOpen) {
    float startNx = 0.0f;
    float startNy = 0.0f;
    if (input.wasTouchSwipeDownInScreen(drawerRenderer, startNx, startNy)) {
      const bool fromTop = startNy <= kOpenFromTopMaxNy;
      INX_SERIAL.printf("[LIGHTDRAWER] swipe-down start nx=%.3f ny=%.3f fromTop=%d\n", startNx, startNy, fromTop);
      if (fromTop) {
        storeDrawerFrame();
        open = true;
        return Action::Opened;
      }
    } else if (input.wasTouchSwipeDownForRenderer(drawerRenderer)) {
      INX_SERIAL.printf("[LIGHTDRAWER] swipe-down with NO start position available\n");
      storeDrawerFrame();
      open = true;
      return Action::Opened;
    }
  }
  return Action::None;
}

LightDrawer::Action LightDrawer::handleTap(const int tapX, const int tapY) const {
  const int lightButtonX = drawerRenderer.getScreenWidth() - lightButtonRightMargin - lightButtonSize;
  const int darkModeButtonX = lightButtonX - lightButtonGap - lightButtonSize;
  if (tapX < 0 || tapX >= drawerRenderer.getScreenWidth() || tapY < 0 || tapY >= lightDrawerHeight()) {
    open = false;
    frameRestorePending = true;
    return Action::Closed;
  }

  if (tapX >= darkModeButtonX && tapX < darkModeButtonX + lightButtonSize && tapY >= lightButtonY &&
      tapY < lightButtonY + lightButtonSize) {



    drawerRenderer.setDarkMode(!drawerRenderer.isDarkMode());
    if (drawerFrameStored) {
      if (drawerRenderer.copyStoredBwToFramebuffer()) {
        drawerRenderer.invertScreen();
        drawerRenderer.freeBwBufferChunks();
        drawerFrameStored = drawerRenderer.storeBwBuffer();
      } else {
        releaseDrawerFrame();
        frameRestorePending = false;
      }
    }
    SETTINGS.darkMode = drawerRenderer.isDarkMode() ? 1 : 0;
    SETTINGS.saveToFile();
    return Action::Opened;
  }

  if (tapX >= lightButtonX && tapX < lightButtonX + lightButtonSize && tapY >= lightButtonY &&
      tapY < lightButtonY + lightButtonSize) {
    frontlight_ui::toggle();
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

  if (!hasTemperatureSlider()) return Action::Opened;

  if (isInCaret(tapX, tapY, lightTemperatureY, false) || isInCaret(tapX, tapY, lightTemperatureY, true)) {
    const bool increase = isInCaret(tapX, tapY, lightTemperatureY, true);
    const int next = frontlight_ui::stepPercent(frontlight.colorTemperature(), increase);
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
