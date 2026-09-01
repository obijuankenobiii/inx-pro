#pragma once

#include "system/MappedInputManager.h"

class GfxRenderer;

namespace navigation {

/** Owns the X4 Pro front-light drawer UI and input handling. */
class LightDrawer {
 public:
  enum class Action { None, Adjusted, Opened, Closed };

  explicit LightDrawer(GfxRenderer& renderer);

  bool isOpen() const { return open; }
  bool isSliderDragging() const { return sliderDragging; }
  void render() const;
  Action handleInput(MappedInputManager& input) const;
  Action handleTap(int tapX, int tapY) const;

 private:
  GfxRenderer& drawerRenderer;
  mutable bool open = false;
  mutable bool sliderDragging = false;
  mutable bool sliderDragChanged = false;
  mutable uint8_t sliderControl = 0;

  /** Whole-frame store held for the duration of a slider drag; see LightDrawer.cpp. */
  mutable bool sliderFrameStored = false;
  mutable int sliderStripTrackY = 0;
  mutable bool sliderRepaintPending = false;

  void refreshAfterSliderChange() const;
  void storeSliderFrame() const;
  void releaseSliderFrame() const;
  void renderSliderRowLive() const;
};

}
