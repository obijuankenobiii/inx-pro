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
  /** Restores the page frame for a drawer-only render, if one was captured. */
  bool restoreStoredFrameForRender() const;
  void render() const;
  Action handleInput(MappedInputManager& input) const;
  Action handleTap(int tapX, int tapY) const;

 private:
  GfxRenderer& drawerRenderer;
  mutable bool open = false;
  mutable bool sliderDragging = false;
  mutable bool sliderDragChanged = false;
  mutable uint8_t sliderControl = 0;

  /** Whole-page frame held while the drawer is open, so its overlay can render without repainting the page. */
  mutable bool drawerFrameStored = false;
  mutable bool frameRestorePending = false;
  mutable bool sliderRepaintPending = false;

  void refreshAfterSliderChange() const;
  void storeDrawerFrame() const;
  void releaseDrawerFrame() const;
  void renderSliderRowLive() const;
};

}
