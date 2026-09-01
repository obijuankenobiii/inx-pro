#pragma once

/**
 * @file JpegRender.h
 * @brief Direct JPEG rendering helpers for page images.
 */

#include <string>

#include "BitmapUtil.h"
#include "ImageRenderMode.h"

#ifdef SIMULATOR
#include <SDCardManager.h>
#else
class FsFile;
#endif
class GfxRenderer;
class Print;

struct JpegLevelCapture {
  uint8_t* values = nullptr;
  size_t capacity = 0;
  int width = 0;
  int height = 0;
  int drawOffsetX = 0;
  int drawOffsetY = 0;
  bool captured = false;
};

class JpegRender {
 public:
  explicit JpegRender(GfxRenderer& renderer) : renderer_(renderer) {}

  bool render(FsFile& jpegFile, int x, int y, int targetWidth, int targetHeight, bool cropToFill = false,
              ImageRenderMode mode = ImageRenderMode::OneBit, bool quality = false,
              JpegLevelCapture* capture = nullptr, float cropAnchorX = 0.5f) const;
  bool fromPath(const std::string& path, int x, int y, int targetWidth, int targetHeight, bool cropToFill = false,
                ImageRenderMode mode = ImageRenderMode::OneBit, bool quality = false,
                JpegLevelCapture* capture = nullptr, float cropAnchorX = 0.5f) const;

  void replayCapture(const JpegLevelCapture& capture, ImageRenderMode mode) const;

  static bool getDimensions(FsFile& jpegFile, int* outW, int* outH);
  static bool getDimensions(const std::string& path, int* outW, int* outH);

  static bool writeProgressiveThumbnailJpeg(FsFile& jpegFile, Print& jpegOut, int targetMaxWidth,
                                             int targetMaxHeight, uint8_t quality);

 private:
  GfxRenderer& renderer_;
};
