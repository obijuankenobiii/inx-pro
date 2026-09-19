#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <functional>

class GfxRenderer;

/** Generates missing book thumbnails without owning a screen or activity. */
class ThumbnailGeneration final {
 public:
  struct Result {
    int processed = 0;
    int generated = 0;
    int skipped = 0;
    int failed = 0;
  };

  static bool generate(GfxRenderer& renderer, SemaphoreHandle_t rendererMutex, Result& result,
                       const std::function<void(int, const char*)>& progress,
                       const std::function<bool()>& shouldCancel = {});
};
