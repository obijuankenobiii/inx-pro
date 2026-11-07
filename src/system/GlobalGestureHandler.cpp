#include "GlobalGestureHandler.h"

#include <GfxRenderer.h>

#include "system/MappedInputManager.h"

#if INX_HAS_FRONTLIGHT || FREEINK_DEVICE_X4PRO || FREEINK_DEVICE_MURPHY_M4 || FREEINK_DEVICE_PICCO
#include "activity/page/navigation/LightDrawer.h"
#endif

namespace {
constexpr float kEdgeExitMaxNx = 0.05f;
constexpr float kBottomExitMinNy = 0.95f;
}

GlobalGestureHandler::GlobalGestureHandler(GfxRenderer& renderer) : renderer_(renderer) {}

bool GlobalGestureHandler::handleInput(MappedInputManager& input, const bool allowGlobalExitGesture,
                                       const bool allowGlobalDrawer,
                                       const std::function<void()>& onGlobalExit,
                                       const std::function<void()>& onGlobalLeftEdgeHome) {
#if INX_HAS_FRONTLIGHT || FREEINK_DEVICE_X4PRO || FREEINK_DEVICE_MURPHY_M4 || FREEINK_DEVICE_PICCO
  auto& drawer = navigation::globalLightDrawer(renderer_);
  const navigation::LightDrawer::Action drawerAction = drawer.handleInput(input, allowGlobalDrawer);

  if (drawerAction == navigation::LightDrawer::Action::None && drawer.isOpen() && input.hasTouch()) {
    float tapNx = 0.0f;
    float tapNy = 0.0f;
    if (input.wasTouchTapInScreen(renderer_, tapNx, tapNy)) {
      const int tapX = static_cast<int>(tapNx * renderer_.getScreenWidth());
      const int tapY = static_cast<int>(tapNy * renderer_.getScreenHeight());
      const navigation::LightDrawer::Action tapAction = drawer.handleTap(tapX, tapY);
      if (tapAction != navigation::LightDrawer::Action::None) {
        if (tapAction == navigation::LightDrawer::Action::Closed) {
          drawer.restoreStoredFrameForRender();
        } else {
          drawer.render();
        }
        renderer_.displayBufferAsync();
        return true;
      }
    }
  }

  if (drawerAction == navigation::LightDrawer::Action::Opened) {
    drawer.render();
    renderer_.displayBufferAsync();
    return true;
  }
  if (drawerAction == navigation::LightDrawer::Action::Closed) {
    drawer.restoreStoredFrameForRender();
    renderer_.displayBufferAsync();
    return true;
  }
  if (drawerAction == navigation::LightDrawer::Action::Adjusted || drawer.isOpen()) return true;
#endif

  if (!allowGlobalExitGesture || !input.hasTouch()) return false;

  float startNx = 0.0f;
  float startNy = 0.0f;
  if (input.wasTouchSwipeUpInScreen(renderer_, startNx, startNy) && startNy >= kBottomExitMinNy) {
    if (onGlobalExit) onGlobalExit();
    return true;
  }
  if (input.wasTouchSwipeRightInScreen(renderer_, startNx, startNy) && startNx <= kEdgeExitMaxNx) {
    if (onGlobalLeftEdgeHome) onGlobalLeftEdgeHome();
    return true;
  }
  return false;
}
