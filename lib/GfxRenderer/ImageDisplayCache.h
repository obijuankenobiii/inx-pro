#pragma once

/**
 * @file ImageDisplayCache.h
 * @brief Raw display-pixel cache for rendered image rectangles.
 */

#include <cstdint>
#include <string>

#include "BitmapRender.h"
#include "ImageRenderMode.h"

class GfxRenderer;

struct ImageDisplayCacheOptions {
  bool cropToFill = false;
  ImageRenderMode mode = ImageRenderMode::OneBit;
  uint8_t renderPlane = 0;
  // Quantized so crop position becomes part of the durable/PSRAM cache key.
  // A centered cover and an edge-cropped carousel cover cannot share pixels.
  uint8_t cropAnchor = 128;
  BitmapRender::RoundedOutside roundedOutside = BitmapRender::RoundedOutside::None;
  bool quality = false;
};

class ImageDisplayCache {
 public:
  static bool renderIfAvailable(GfxRenderer& renderer, const std::string& sourcePath, int x, int y, int width,
                                int height, const ImageDisplayCacheOptions& options);
  /** Load a durable display cache entry into the PSRAM front cache without drawing it. */
  static bool preloadIfAvailable(GfxRenderer& renderer, const std::string& sourcePath, int x, int y, int width,
                                 int height, const ImageDisplayCacheOptions& options);
  static bool displayTwoBitIfAvailable(GfxRenderer& renderer, const std::string& sourcePath, int x, int y, int width,
                                       int height, const ImageDisplayCacheOptions& options, bool quality = false,
                                       bool fastQuality = false);
  static bool hasCachedTwoBit(GfxRenderer& renderer, const std::string& sourcePath, int x, int y, int width, int height,
                              const ImageDisplayCacheOptions& options, bool quality = false);

  /**
   * One-file grayscale cache: both bit-planes live in a single entry (one hash, one SD open), the same
   * shape as XTC's own page format (one read gets both planes) instead of two independently-hashed cache
   * files. `options.renderPlane` is ignored for these - a combined entry isn't plane-specific.
   */
  static bool hasCombinedTwoBit(GfxRenderer& renderer, const std::string& sourcePath, int x, int y, int width,
                                int height, const ImageDisplayCacheOptions& options);
  /** Reads both planes from the one combined file and drives the full two-pass grayscale display, same
   * contract as displayTwoBitIfAvailable() - single SD open instead of two. */
  static bool renderCombinedTwoBit(GfxRenderer& renderer, const std::string& sourcePath, int x, int y, int width,
                                   int height, const ImageDisplayCacheOptions& options, bool quality,
                                   bool fastQuality);
  /** Captures the plane currently in the framebuffer into an in-memory scratch slot (call once per pass,
   * right after rendering it, before the next pass overwrites the framebuffer). */
  static bool captureTwoBitPlane(GfxRenderer& renderer, int x, int y, int width, int height, bool isMsbPlane);
  /** Writes both previously-captured planes as one combined cache entry and clears the scratch slots.
   * No-op (returns false) unless both planes were captured first. */
  static bool commitTwoBitCombined(GfxRenderer& renderer, const std::string& sourcePath, int x, int y, int width,
                                   int height, const ImageDisplayCacheOptions& options);
  /** Discards any in-progress capture without writing it (e.g. a failed/aborted render). */
  static void cancelTwoBitCapture();

  static bool store(GfxRenderer& renderer, const std::string& sourcePath, int x, int y, int width, int height,
                    const ImageDisplayCacheOptions& options);
  static bool storeAsync(GfxRenderer& renderer, const std::string& sourcePath, int x, int y, int width, int height,
                         const ImageDisplayCacheOptions& options);
  /**
   * Writes at most `maximumJobs` previously captured cache planes. Call this
   * from the main loop after UI work: the pixels are already usable from the
   * PSRAM cache, and SD/SPI remains owned by the loop task.
   */
  static void flushDeferredWrites(uint8_t maximumJobs = 1);
  static bool hasCached(GfxRenderer& renderer, const std::string& sourcePath, int x, int y, int width, int height,
                        const ImageDisplayCacheOptions& options);

 private:
  static std::string pathFor(GfxRenderer& renderer, const std::string& sourcePath, int x, int y, int width, int height,
                             const ImageDisplayCacheOptions& options);
  static std::string pathForCombined(GfxRenderer& renderer, const std::string& sourcePath, int x, int y, int width,
                                     int height, const ImageDisplayCacheOptions& options);
};
