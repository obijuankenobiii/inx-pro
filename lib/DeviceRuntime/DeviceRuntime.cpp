#include "DeviceRuntime.h"

#include <HalDisplay.h>
#include <HalGPIO.h>

#include <GfxRenderer.h>

#include "state/SystemSetting.h"
#include "system/MappedInputManager.h"
#include "system/Frontlight.h"

#if FREEINK_DEVICE_X4PRO
#include <XteinkDetect.h>
#endif

#if FREEINK_DEVICE_X4PRO || FREEINK_DEVICE_MURPHY_M4 || FREEINK_DEVICE_PICCO
FrontlightManager frontlight;
#endif

namespace DeviceRuntime {

void initialize(HalGPIO& gpio) {
#if FREEINK_DEVICE_X4PRO
  freeink::applyXteinkDisplayController();
#endif

#if FREEINK_DEVICE_X4PRO || FREEINK_DEVICE_MURPHY_M4 || FREEINK_DEVICE_PICCO
  frontlight.begin();
#endif

#if FREEINK_DEVICE_PICCO
  // FrontlightManager and Picco's touch controller share the I2C bus. Restore
  // the touch bus after the frontlight probe has completed.
  gpio.reinitializeTouch();
#endif
}

void beforeActivitySwitch(HalDisplay& display, GfxRenderer& renderer, MappedInputManager& input) {
#if FREEINK_DEVICE_MURPHY_M4
  // Murphy's async dual-buffer display must finish before the next activity
  // copies the currently displayed frame into its write buffer.
  display.finishPendingRefresh();
  renderer.syncWriteBufferFromActive();
#endif

#if FREEINK_DEVICE_X4PRO || FREEINK_DEVICE_MURPHY_M4
  input.discardPendingTouchTap();
  input.discardPendingSwipe();
  input.ignoreCurrentTouch();
#endif

  (void)display;
  (void)renderer;
  (void)input;
}

void startDeepSleep(HalGPIO& gpio) {
#if FREEINK_DEVICE_PICCO
  gpio.startDeepSleep(SETTINGS.piccoToggleMode == SystemSetting::PICCO_TOGGLE_POWER && gpio.hasToggleSwitch() &&
                      !gpio.isToggleSwitchOn());
#else
  gpio.startDeepSleep();
#endif
}

void refreshAfterPowerButton(HalDisplay& display, GfxRenderer& renderer) {
  renderer.syncWriteBufferFromActive();
#if FREEINK_DEVICE_X4PRO || FREEINK_DEVICE_MURPHY_M4
  display.displayBuffer(HalDisplay::FULL_REFRESH);
#else
  display.displayBuffer(HalDisplay::HALF_REFRESH);
#endif
}

void poll(HalGPIO& gpio, const std::function<void()>& enterDeepSleep) {
#if FREEINK_DEVICE_PICCO
  static bool synchronized = false;
  static uint8_t lastMode = 0xff;
  if (!gpio.hasToggleSwitch()) return;

  const bool modeChanged = lastMode != SETTINGS.piccoToggleMode;
  const bool switchChanged = gpio.wasToggleSwitchChanged();
  if (SETTINGS.piccoToggleMode == SystemSetting::PICCO_TOGGLE_POWER) {
    if (!gpio.isToggleSwitchOn()) {
      INX_SERIAL.println("[INPUT][POWER-SWITCH] off -> deep sleep");
      enterDeepSleep();
      return;
    }
    if (!synchronized || modeChanged || switchChanged) {
      INX_SERIAL.println("[INPUT][POWER-SWITCH] on");
    }
    synchronized = true;
    lastMode = SETTINGS.piccoToggleMode;
    return;
  }

  if (synchronized && !switchChanged && !modeChanged) return;
  if (gpio.isToggleSwitchOn()) {
    frontlight.on();
  } else {
    frontlight.off();
  }
  frontlight_ui::persist();
  INX_SERIAL.printf("[INPUT][LIGHT-SWITCH] %s\n", gpio.isToggleSwitchOn() ? "on" : "off");
  synchronized = true;
  lastMode = SETTINGS.piccoToggleMode;
#else
  (void)gpio;
  (void)enterDeepSleep;
#endif
}

}  // namespace DeviceRuntime
