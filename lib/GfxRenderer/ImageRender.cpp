/**
 * @file ImageRender.cpp
 * @brief Definitions for ImageRender.
 */

#include "ImageRender.h"

#include <Arduino.h>
#include <HardwareSerial.h>
#include <SDCardManager.h>

#include <algorithm>
#include <cmath>
#include <memory>

#include "../../src/util/StringUtils.h"
#include "Bitmap.h"
#include "BitmapUtil.h"
#include "GfxRenderer.h"
#include "ImageDisplayCache.h"
#include "../../src/util/SdIoMutex.h"
#include "JpegRender.h"
#include "GifRender.h"
#include "PngRender.h"

namespace {

uint8_t cacheCropAnchor(const float value) {
  return static_cast<uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
}

class ImageRenderScope {
 public:
  explicit ImageRenderScope(GfxRenderer& renderer) : renderer_(renderer), previous_(renderer.isImageRendering()) {
    renderer_.setImageRendering(true);
  }

  ~ImageRenderScope() { renderer_.setImageRendering(previous_); }

 private:
  GfxRenderer& renderer_;
  bool previous_;
};

}  // namespace

ImageRender ImageRender::create(GfxRenderer& renderer, const std::string& path) {
  return ImageRender(renderer, path, detectFormat(path));
}

ImageRender::Format ImageRender::detectFormat(const std::string& path) {
  if (StringUtils::checkFileExtension(path, ".jpg") || StringUtils::checkFileExtension(path, ".jpeg")) {
    return Format::Jpeg;
  }
  if (StringUtils::checkFileExtension(path, ".png")) {
    return Format::Png;
  }
  if (StringUtils::checkFileExtension(path, ".gif")) {
    return Format::Gif;
  }
  return Format::Bitmap;
}

bool ImageRender::getDimensions(const std::string& path, int* outW, int* outH) {
  return getDimensionsForFormat(path, detectFormat(path), outW, outH);
}

bool ImageRender::getDimensions(int* outW, int* outH) const {
  return getDimensionsForFormat(path_, format_, outW, outH);
}

bool ImageRender::getDimensionsForFormat(const std::string& path, Format format, int* outW, int* outH) {
  SdIoMutex::Lock ioLock;
  if (format == Format::Jpeg) {
    return JpegRender::getDimensions(path, outW, outH);
  }
  if (format == Format::Png) {
    return PngRender::getDimensions(path, outW, outH);
  }
  if (format == Format::Gif) {
    FsFile gif;
    if (!SdMan.openFileForRead("EHP", path, gif)) return false;
    const bool ok = GifRender::getDimensions(gif, outW, outH);
    gif.close();
    return ok;
  }

  FsFile file;
  if (!SdMan.openFileForRead("EHP", path, file)) {
    return false;
  }

  Bitmap bitmap(file);
  const bool ok = bitmap.parseHeaders() == BmpReaderError::Ok;
  if (ok) {
    *outW = bitmap.getWidth();
    *outH = bitmap.getHeight();
  }
  file.close();
  return ok;
}

bool ImageRender::render(int x, int y, int width, int height, const Options& options) const {
  return render(x, y, width, height, options, nullptr);
}

bool ImageRender::renderDisplayCacheOnly(int x, int y, int width, int height, const Options& options) const {
  ImageRenderScope imageScope(renderer_);
  ImageDisplayCacheOptions cacheOptions;
  cacheOptions.cropToFill = options.cropToFill;
  cacheOptions.cropAnchor = cacheCropAnchor(options.cropAnchorX);
  cacheOptions.mode = options.mode;
  cacheOptions.renderPlane = static_cast<uint8_t>(renderer_.getRenderMode());
  cacheOptions.roundedOutside = options.roundedOutside;
  cacheOptions.quality = options.quality;
  const bool canUseDisplayCache =
      options.useDisplayCache &&
      ((options.mode == ImageRenderMode::OneBit && renderer_.getRenderMode() == GfxRenderer::BW) ||
       options.mode == ImageRenderMode::TwoBit);
  if (!canUseDisplayCache) {
    return false;
  }
  return ImageDisplayCache::renderIfAvailable(renderer_, path_, x, y, width, height, cacheOptions);
}

bool ImageRender::preloadDisplayCache(int x, int y, int width, int height, const Options& options) const {
  ImageRenderScope imageScope(renderer_);
  ImageDisplayCacheOptions cacheOptions;
  cacheOptions.cropToFill = options.cropToFill;
  cacheOptions.cropAnchor = cacheCropAnchor(options.cropAnchorX);
  cacheOptions.mode = options.mode;
  cacheOptions.renderPlane = static_cast<uint8_t>(renderer_.getRenderMode());
  cacheOptions.roundedOutside = options.roundedOutside;
  cacheOptions.quality = options.quality;
  const bool canUseDisplayCache =
      options.useDisplayCache &&
      ((options.mode == ImageRenderMode::OneBit && renderer_.getRenderMode() == GfxRenderer::BW) ||
       options.mode == ImageRenderMode::TwoBit);
  return canUseDisplayCache &&
         ImageDisplayCache::preloadIfAvailable(renderer_, path_, x, y, width, height, cacheOptions);
}

bool ImageRender::render(int x, int y, int width, int height, const Options& options,
                         JpegLevelCapture* jpegCapture) const {
  ImageRenderScope imageScope(renderer_);
  Options effectiveOptions = options;
  if (format_ == Format::Png) {
    // PNGs are intentionally always rendered through the low/1-bit path.
    // This keeps EPUB PNGs out of the medium/high image waveform even when
    // the caller requests TwoBit or quality rendering for the whole page.
    effectiveOptions.mode = ImageRenderMode::OneBit;
    effectiveOptions.quality = false;
    effectiveOptions.fastQuality = false;
    // The one-bit PNG path only writes ink pixels. Grayscale page planes may
    // start dark, so clear just the image area without changing the caller's
    // render mode.
    if (renderer_.getRenderMode() != GfxRenderer::BW) {
      renderer_.rectangle.fill(x, y, width, height, false);
    }
  }
  ImageDisplayCacheOptions cacheOptions;
  cacheOptions.cropToFill = effectiveOptions.cropToFill;
  cacheOptions.cropAnchor = cacheCropAnchor(effectiveOptions.cropAnchorX);
  cacheOptions.mode = effectiveOptions.mode;
  cacheOptions.renderPlane = static_cast<uint8_t>(renderer_.getRenderMode());
  cacheOptions.roundedOutside = effectiveOptions.roundedOutside;
  cacheOptions.quality = effectiveOptions.quality;
  const bool canUseDisplayCache =
      effectiveOptions.useDisplayCache &&
      ((effectiveOptions.mode == ImageRenderMode::OneBit && renderer_.getRenderMode() == GfxRenderer::BW) ||
       effectiveOptions.mode == ImageRenderMode::TwoBit);
  // Skip the on-disk raster cache lookup only when replaying a capture (nothing to look up for - we
  // already have the pixels in memory). On the first (capture) call, jpegCapture->captured is still
  // false here, so a cache hit is still preferred over decoding at all.
  if (canUseDisplayCache && !(jpegCapture && jpegCapture->captured)) {
    const bool cacheHit = ImageDisplayCache::renderIfAvailable(renderer_, path_, x, y, width, height, cacheOptions);
    if (cacheHit) {
      return true;
    }
  }

  bool ok = false;
  {
    // Protect only the source decode. Durable cache persistence runs later at
    // idle priority, so page rendering never waits for a cache-file write.
    SdIoMutex::Lock ioLock;
    if (jpegCapture && jpegCapture->captured) {
      // The packed values are display levels, not JPEG-specific pixels. JPEG
      // and PNG can both replay them for the second grayscale plane.
      JpegRender(renderer_).replayCapture(*jpegCapture, effectiveOptions.mode);
      ok = true;
    } else if (format_ == Format::Jpeg) {
      JpegRender jpeg(renderer_);
      ok = jpeg.fromPath(path_, x, y, width, height, effectiveOptions.cropToFill, effectiveOptions.mode,
                         effectiveOptions.quality, jpegCapture, effectiveOptions.cropAnchorX);
    } else if (format_ == Format::Gif) {
      FsFile gif;
      if (SdMan.openFileForRead("EHP", path_, gif)) {
        GifRender gif_(renderer_);
        ok = gif_.render(gif, x, y, width, height, effectiveOptions.cropToFill, effectiveOptions.mode,
                         effectiveOptions.quality, jpegCapture, effectiveOptions.cropAnchorX);
        gif.close();
      }
    } else if (format_ == Format::Png) {
      PngRender png(renderer_);
      ok = png.fromPath(path_, x, y, width, height, effectiveOptions.cropToFill, effectiveOptions.mode,
                        effectiveOptions.cropAnchorX, jpegCapture);
    } else {
      FsFile file;
      if (!SdMan.openFileForRead("EHP", path_, file)) {
        INX_SERIAL.printf("[PAGEIMG] Failed to open image file: %s\n", path_.c_str());
        return false;
      }

      Bitmap bitmap(file);
      ok = bitmap.parseHeaders() == BmpReaderError::Ok;
      if (ok) {
        float cropX = 0.f;
        float cropY = 0.f;
        if (effectiveOptions.cropToFill && bitmap.getWidth() > 0 && bitmap.getHeight() > 0 && width > 0 && height > 0) {
          const float imageRatio = static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
          const float targetRatio = static_cast<float>(width) / static_cast<float>(height);
          if (imageRatio > targetRatio) {
            cropX = 1.0f - (targetRatio / imageRatio);
          } else {
            cropY = 1.0f - (imageRatio / targetRatio);
          }
        }
        renderer_.bitmap.render(bitmap, x, y, width, height, cropX, cropY, effectiveOptions.roundedOutside,
                                effectiveOptions.mode, effectiveOptions.cropAnchorX);
      }
      file.close();
    }
  }

  if (ok && effectiveOptions.roundedOutside != BitmapRender::RoundedOutside::None) {
    renderer_.bitmap.maskRoundedOutside(x, y, width, height, effectiveOptions.roundedOutside);
  }

  if (ok && canUseDisplayCache) {
    if (effectiveOptions.asyncDisplayCache) {
      ImageDisplayCache::storeAsync(renderer_, path_, x, y, width, height, cacheOptions);
    } else {
      ImageDisplayCache::store(renderer_, path_, x, y, width, height, cacheOptions);
    }
  }
  return ok;
}

bool ImageRender::displayCachedTwoBit(int x, int y, int width, int height, const Options& options,
                                      const bool quality) const {
  ImageRenderScope imageScope(renderer_);
  if (!options.useDisplayCache) {
    return false;
  }
  const bool effectiveQuality = quality && format_ != Format::Png;
  ImageDisplayCacheOptions cacheOptions;
  cacheOptions.cropToFill = options.cropToFill;
  cacheOptions.cropAnchor = cacheCropAnchor(options.cropAnchorX);
  cacheOptions.mode = ImageRenderMode::TwoBit;
  cacheOptions.roundedOutside = options.roundedOutside;
  cacheOptions.quality = effectiveQuality;
  const bool hit = ImageDisplayCache::displayTwoBitIfAvailable(renderer_, path_, x, y, width, height, cacheOptions,
                                                               effectiveQuality, options.fastQuality);
  return hit;
}

bool ImageRender::hasCachedTwoBit(int x, int y, int width, int height, const Options& options,
                                  const bool quality) const {
  if (!options.useDisplayCache) {
    return false;
  }
  const bool effectiveQuality = quality && format_ != Format::Png;
  ImageDisplayCacheOptions cacheOptions;
  cacheOptions.cropToFill = options.cropToFill;
  cacheOptions.cropAnchor = cacheCropAnchor(options.cropAnchorX);
  cacheOptions.mode = ImageRenderMode::TwoBit;
  cacheOptions.roundedOutside = options.roundedOutside;
  cacheOptions.quality = effectiveQuality;
  return ImageDisplayCache::hasCachedTwoBit(renderer_, path_, x, y, width, height, cacheOptions, effectiveQuality);
}

bool ImageRender::hasCachedGrayscale(int x, int y, int width, int height, const Options& options,
                                     const bool quality) const {
  if (!options.useDisplayCache) {
    return false;
  }
  const bool effectiveQuality = quality && format_ != Format::Png;
  ImageDisplayCacheOptions cacheOptions;
  cacheOptions.cropToFill = options.cropToFill;
  cacheOptions.cropAnchor = cacheCropAnchor(options.cropAnchorX);
  cacheOptions.roundedOutside = options.roundedOutside;
  cacheOptions.quality = effectiveQuality;
  return ImageDisplayCache::hasCombinedTwoBit(renderer_, path_, x, y, width, height, cacheOptions);
}

bool ImageRender::displayGrayscale(int x, int y, int width, int height, const Options& options,
                                   const bool quality, const std::function<void()>& overlay) const {
  ImageRenderScope imageScope(renderer_);

  // PNGs are always rendered as low-quality 1-bit images. They do not enter the
  // medium/high grayscale passes or create/read two-bit grayscale cache entries.
  if (format_ == Format::Png) {
    Options lowOptions = options;
    lowOptions.mode = ImageRenderMode::OneBit;
    lowOptions.quality = false;
    lowOptions.fastQuality = false;
    renderer_.setRenderMode(GfxRenderer::BW);
    renderer_.clearScreen(0xFF);
    const bool rendered = render(x, y, width, height, lowOptions);
    if (!rendered) return false;
    if (overlay) overlay();
    renderer_.displayBuffer();
    return true;
  }

  const bool effectiveQuality = quality && format_ != Format::Png;
  Options opt = options;
  opt.mode = ImageRenderMode::TwoBit;
  opt.quality = effectiveQuality;
  opt.useDisplayCache = true;

  ImageDisplayCacheOptions combinedCacheOptions;
  combinedCacheOptions.cropToFill = opt.cropToFill;
  combinedCacheOptions.cropAnchor = cacheCropAnchor(opt.cropAnchorX);
  combinedCacheOptions.roundedOutside = opt.roundedOutside;
  combinedCacheOptions.quality = effectiveQuality;

  // A combined entry holds both bit-planes in one file - one hash, one SD open - the same shape XTC uses
  // for its own page reads, instead of the old two-separately-hashed-file lookup.
  if (!overlay && options.useDisplayCache &&
      ImageDisplayCache::renderCombinedTwoBit(renderer_, path_, x, y, width, height, combinedCacheOptions,
                                              effectiveQuality, opt.fastQuality)) {
    return true;  // served from cache (handles both planes + refresh + cleanup)
  }

  const bool captureForCombinedCache = options.useDisplayCache;
  Options renderOpt = opt;
  // The combined-cache capture below replaces the old per-plane cache read/store, so render() should
  // neither look up nor persist a legacy single-plane entry for these two passes.
  renderOpt.useDisplayCache = false;

  auto captureCurrentPlane = [&] {
    if (!captureForCombinedCache) {
      return;
    }
    const bool isMsbPlane =
        renderer_.getRenderMode() == GfxRenderer::GRAY2_MSB || renderer_.getRenderMode() == GfxRenderer::GRAYSCALE_MSB;
    ImageDisplayCache::captureTwoBitPlane(renderer_, x, y, width, height, isMsbPlane);
  };

  // JPEGs decode via a slow SD read + DCT pass; renderGrayscalePasses below calls its drawPlane lambda
  // once per plane (LSB, then MSB), and a plain render() re-decodes the whole file each time. Capture the
  // first pass's per-pixel dither level and replay it for the second pass instead - same visual result,
  // no second decode. The capture is packed 2 bits/pixel (4 pixels/byte - see JpegLevelCapture), so this
  // covers a full screen-sized image in a bounded, freed-before-return buffer; oversized images just fall
  // through to the normal double-decode path below.
  constexpr size_t kMaxCapturePixels = 400000;
  const size_t capturePixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
  bool rendered = false;
  if (format_ != Format::Bitmap && capturePixelCount > 0 && capturePixelCount <= kMaxCapturePixels) {
    const size_t neededBytes = (capturePixelCount + 3) / 4;
    std::unique_ptr<uint8_t[]> captureBuffer(new (std::nothrow) uint8_t[neededBytes]);
    if (captureBuffer) {
      JpegLevelCapture capture;
      capture.values = captureBuffer.get();
      capture.capacity = neededBytes;

      renderer_.renderGrayscalePasses(
          effectiveQuality, /*preserveText=*/false,
          [&] {
            renderer_.clearScreen(effectiveQuality ? 0xFF : 0x00);
            render(x, y, width, height, renderOpt, &capture);
            captureCurrentPlane();
            if (overlay) {
              overlay();
            }
          },
          opt.fastQuality);
      rendered = true;
    }
  }

  if (!rendered) {
    renderer_.renderGrayscalePasses(
        effectiveQuality, /*preserveText=*/false,
        [&] {
          renderer_.clearScreen(effectiveQuality ? 0xFF : 0x00);
          render(x, y, width, height, renderOpt);  // renders into the current plane's render mode
          captureCurrentPlane();
          if (overlay) {
            overlay();
          }
        },
        opt.fastQuality);
  }

  if (captureForCombinedCache) {
    // Persists whatever both passes just captured (decode -> cache to raster -> display, one file).
    ImageDisplayCache::commitTwoBitCombined(renderer_, path_, x, y, width, height, combinedCacheOptions);
  }

  return true;
}

void ImageRender::displayGrayscale(GfxRenderer& renderer, const bool quality, const bool preserveText,
                                   const std::function<void()>& drawPlane, const bool fastQuality) {
  renderer.renderGrayscalePasses(quality, preserveText, drawPlane, fastQuality);
}

bool ImageRender::render(int x, int y, int width, int height) const { return render(x, y, width, height, Options()); }

bool ImageRender::render(int x, int y, int width, int height, ImageRenderMode mode) const {
  Options options;
  options.mode = mode;
  return render(x, y, width, height, options);
}
