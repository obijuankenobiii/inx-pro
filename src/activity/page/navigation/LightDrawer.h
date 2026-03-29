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
  void render() const;
  Action handleInput(MappedInputManager& input) const;
  Action handleTap(int tapX, int tapY) const;

 private:
  GfxRenderer& drawerRenderer;
  mutable bool open = false;
};

}  // namespace navigation
