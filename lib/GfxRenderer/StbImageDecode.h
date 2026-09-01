#pragma once

#include "ImageRenderMode.h"

class FsFile;
class GfxRenderer;
struct JpegLevelCapture;

namespace StbImageDecode {

bool render(FsFile& file, GfxRenderer& renderer, int x, int y, int targetWidth, int targetHeight, bool cropToFill,
            ImageRenderMode mode, bool quality, JpegLevelCapture* capture, float cropAnchorX = 0.5f);

bool getDimensions(FsFile& file, int* outW, int* outH);

}
