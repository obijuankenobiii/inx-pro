#pragma once

/**
 * @file ImageRender.h
 * @brief Factory-style dispatch for rendering cached page images.
 */

#include <Bitmap.h>
#include <BitmapRender.h>
#include <ImageRenderMode.h>

#include <functional>
#include <string>

class GfxRenderer;
struct JpegLevelCapture;

class ImageRender {
 public:
  struct Options {
    ImageRenderMode mode = ImageRenderMode::OneBit;
    bool cropToFill = false;
    BitmapRender::RoundedOutside roundedOutside = BitmapRender::RoundedOutside::None;
    bool useDisplayCache = true;
    // Queue the packed display-cache write for the main loop's idle phase
    // instead of blocking the caller after the image has been rendered.
    bool asyncDisplayCache = false;
    bool quality = false;
    bool fastQuality = false;
    // Horizontal position of a crop-to-fill image: 0 = left, 0.5 = center, 1 = right.
    float cropAnchorX = 0.5f;
  };

  static ImageRender create(GfxRenderer& renderer, const std::string& path);
  static bool getDimensions(const std::string& path, int* outW, int* outH);

  bool getDimensions(int* outW, int* outH) const;
  bool render(int x, int y, int width, int height) const;
  bool render(int x, int y, int width, int height, const Options& options) const;
  bool render(int x, int y, int width, int height, ImageRenderMode mode) const;
  bool renderDisplayCacheOnly(int x, int y, int width, int height, const Options& options) const;
  /** Promotes an existing durable display cache entry into PSRAM without changing the framebuffer. */
  bool preloadDisplayCache(int x, int y, int width, int height, const Options& options) const;
  // Same as above, but for JPEG/PNG, threads a level capture through so a caller doing a two-pass
  // grayscale render can decode once (first call fills `jpegCapture`) and replay for the second
  // pass (subsequent call, when `jpegCapture->captured` is already true, skips decoding entirely).
  // No-op for bitmap formats.
  bool render(int x, int y, int width, int height, const Options& options, JpegLevelCapture* jpegCapture) const;
  bool displayCachedTwoBit(int x, int y, int width, int height, const Options& options, bool quality = false) const;
  bool hasCachedTwoBit(int x, int y, int width, int height, const Options& options, bool quality = false) const;
  // Checks the combined (single-file, both-planes) grayscale cache that displayGrayscale() below reads
  // and writes - use this, not hasCachedTwoBit(), to decide whether a displayGrayscale() call will hit.
  bool hasCachedGrayscale(int x, int y, int width, int height, const Options& options, bool quality = false) const;
  // Full-screen 2-bit grayscale display in ONE call: serves from the display cache if present, otherwise
  // renders both planes (storing them) and drives the gray refresh, then resets BW mode + a clean baseline.
  // `quality` selects the quality LUT (GRAY2) vs the fast LUT (GRAYSCALE). Used by the sleep screen.
  bool displayGrayscale(int x, int y, int width, int height, const Options& options, bool quality,
                        const std::function<void()>& overlay = {}) const;

  // General 2-bit grayscale display: runs both planes via `drawPlane` (which populates the framebuffer for the
  // current plane), drives the gray refresh, and resets to BW. This is the single entry point shared by the
  // book reader (text-preserving: preserveText=true, drawPlane rebuilds inverted text + image overlay) and any
  // other custom grayscale composite.
  static void displayGrayscale(GfxRenderer& renderer, bool quality, bool preserveText,
                               const std::function<void()>& drawPlane, bool fastQuality = false);

 private:
  enum class Format { Bitmap, Jpeg, Png, Gif };

  ImageRender(GfxRenderer& renderer, const std::string& path, Format format)
      : renderer_(renderer), path_(path), format_(format) {}

  static Format detectFormat(const std::string& path);
  static bool getDimensionsForFormat(const std::string& path, Format format, int* outW, int* outH);

  GfxRenderer& renderer_;
  const std::string& path_;
  Format format_;
};
