#pragma once

#include <functional>

class GfxRenderer;
class HalDisplay;
class HalGPIO;
class MappedInputManager;

/** Device-family lifecycle hooks kept out of the application entry point. */
namespace DeviceRuntime {

void initialize(HalGPIO& gpio);
void beforeActivitySwitch(HalDisplay& display, GfxRenderer& renderer, MappedInputManager& input);
void startDeepSleep(HalGPIO& gpio);
void refreshAfterPowerButton(HalDisplay& display, GfxRenderer& renderer);
void poll(HalGPIO& gpio, const std::function<void()>& enterDeepSleep);

}  // namespace DeviceRuntime
