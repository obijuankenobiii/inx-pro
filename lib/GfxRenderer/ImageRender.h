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
    bool asyncDisplayCache = false;
    bool quality = false;
    bool fastQuality = false;
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
  bool render(int x, int y, int width, int height, const Options& options, JpegLevelCapture* jpegCapture) const;
  bool displayCachedTwoBit(int x, int y, int width, int height, const Options& options, bool quality = false) const;
  bool hasCachedTwoBit(int x, int y, int width, int height, const Options& options, bool quality = false) const;
  bool hasCachedGrayscale(int x, int y, int width, int height, const Options& options, bool quality = false) const;
  bool displayGrayscale(int x, int y, int width, int height, const Options& options, bool quality,
                        const std::function<void()>& overlay = {}) const;

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
