#pragma once

class GfxRenderer;

/** Home widget that displays a user-selected image from the SD card. */
class ImageWidget final {
 public:
  explicit ImageWidget(GfxRenderer& renderer) : renderer_(renderer) {}

  void render(int x, int y, int width, int height, const char* path, bool cropToFill = false,
              bool padding = false, float cropAnchorX = 0.5f, float cropAnchorY = 0.5f) const;

 private:
  GfxRenderer& renderer_;
};
