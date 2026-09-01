#pragma once

#include <string>

#include "ImageRenderMode.h"

class FsFile;
class GfxRenderer;
struct JpegLevelCapture;

class GifRender {
 public:
  explicit GifRender(GfxRenderer& renderer) : renderer_(renderer) {}

  bool render(FsFile& gifFile, int x, int y, int targetWidth, int targetHeight, bool cropToFill = false,
              ImageRenderMode mode = ImageRenderMode::OneBit, bool quality = false,
              JpegLevelCapture* capture = nullptr, float cropAnchorX = 0.5f);
  bool fromPath(const std::string& path, int x, int y, int targetWidth, int targetHeight, bool cropToFill = false,
                ImageRenderMode mode = ImageRenderMode::OneBit, bool quality = false,
                JpegLevelCapture* capture = nullptr, float cropAnchorX = 0.5f);

  static bool getDimensions(FsFile& gifFile, int* outW, int* outH);
  static bool getDimensions(const std::string& path, int* outW, int* outH);

 private:
  GfxRenderer& renderer_;
};
