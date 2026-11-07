#pragma once

#include <functional>

class GfxRenderer;
class MappedInputManager;

/** Dispatches gestures that are available independently of the active activity. */
class GlobalGestureHandler final {
 public:
  explicit GlobalGestureHandler(GfxRenderer& renderer);

  /**
   * Handles the global light drawer and edge-exit gestures.
   * Returns true when the active activity must not process this input cycle.
   */
  bool handleInput(MappedInputManager& input, bool allowGlobalExitGesture, bool allowGlobalDrawer,
                   const std::function<void()>& onGlobalExit,
                   const std::function<void()>& onGlobalLeftEdgeHome);

 private:
  GfxRenderer& renderer_;
};
