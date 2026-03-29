#pragma once

class GfxRenderer;

class ShortcutList final {
 public:
  explicit ShortcutList(GfxRenderer& renderer) : renderer_(renderer) {}

  void render(int x, int y, int width, int height, int rowHeightOverride = 0) const;
  int hitTest(int x, int y, int areaX, int areaY, int areaW, int areaH, int rowHeightOverride = 0) const;

 private:
  GfxRenderer& renderer_;
};
