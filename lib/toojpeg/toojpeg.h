

#pragma once

namespace TooJpeg {
typedef void (*WRITE_ONE_BYTE)(unsigned char);

bool writeJpeg(WRITE_ONE_BYTE output, const void* pixels, unsigned short width, unsigned short height,
               bool isRGB = true, unsigned char quality = 90, bool downsample = false, const char* comment = nullptr);
}

