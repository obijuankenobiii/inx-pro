#pragma once

#include "ImageRenderMode.h"

class FsFile;
class GfxRenderer;
struct JpegLevelCapture;

// Shared entry onto the stb_image decoder. stb's single STB_IMAGE_IMPLEMENTATION lives in
// JpegRender.cpp (a second one would duplicate symbols), but the decode itself is format-agnostic -
// stbi_load_from_callbacks() sniffs the format and yields 8-bit grayscale - so it is exposed here as a
// neutral API rather than as methods on JpegRender. Any format stb is compiled for (JPEG, GIF) uses it.
//
// Streams through FsFile rather than reading the file into memory; the decode buffer is allocated from
// PSRAM and released before returning, so nothing is retained between calls.
namespace StbImageDecode {

bool render(FsFile& file, GfxRenderer& renderer, int x, int y, int targetWidth, int targetHeight, bool cropToFill,
            ImageRenderMode mode, bool quality, JpegLevelCapture* capture, float cropAnchorX = 0.5f);

bool getDimensions(FsFile& file, int* outW, int* outH);

}  // namespace StbImageDecode
