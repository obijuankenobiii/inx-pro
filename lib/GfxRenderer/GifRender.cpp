#include "GifRender.h"

#include <SDCardManager.h>
#include <SdFat.h>

#include "StbImageDecode.h"

bool GifRender::render(FsFile& gifFile, const int x, const int y, const int targetWidth, const int targetHeight,
                       const bool cropToFill, const ImageRenderMode mode, const bool quality,
                       JpegLevelCapture* capture, const float cropAnchorX) {
  return StbImageDecode::render(gifFile, renderer_, x, y, targetWidth, targetHeight, cropToFill, mode, quality,
                                capture, cropAnchorX);
}

bool GifRender::fromPath(const std::string& path, const int x, const int y, const int targetWidth,
                         const int targetHeight, const bool cropToFill, const ImageRenderMode mode,
                         const bool quality, JpegLevelCapture* capture, const float cropAnchorX) {
  FsFile file;
  if (!SdMan.openFileForRead("GIF", path, file)) return false;
  const bool ok = render(file, x, y, targetWidth, targetHeight, cropToFill, mode, quality, capture, cropAnchorX);
  file.close();
  return ok;
}

bool GifRender::getDimensions(FsFile& gifFile, int* outW, int* outH) {
  return StbImageDecode::getDimensions(gifFile, outW, outH);
}

bool GifRender::getDimensions(const std::string& path, int* outW, int* outH) {
  FsFile file;
  if (!SdMan.openFileForRead("GIF", path, file)) return false;
  const bool ok = StbImageDecode::getDimensions(file, outW, outH);
  file.close();
  return ok;
}
