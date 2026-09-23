#include "ThumbnailGeneration.h"

#include <Epub.h>
#include <GfxRenderer.h>
#include <ImageRender.h>
#include <Pdf.h>
#include <SDCardManager.h>
#include <Xtc.h>

#include <cstring>
#include <string>

#include "activity/page/views/Library/Thumb.h"
#include "util/StringUtils.h"

namespace {
struct Generator {
  GfxRenderer& renderer;
  SemaphoreHandle_t rendererMutex;
  ThumbnailGeneration::Result& result;
  const std::function<void(int, const char*)>& progress;
  const std::function<bool()>& shouldCancel;
  char currentPath[256] = {};

  bool cancelled() const { return shouldCancel && shouldCancel(); }

  void report() const {
    if (progress) progress(result.processed, currentPath);
  }

  bool lockRenderer(const TickType_t timeout) const {
    return rendererMutex && xSemaphoreTake(rendererMutex, timeout) == pdTRUE;
  }

  void precache(const std::string& path) {
    int width = 0;
    int height = 0;
    views::library::Thumb::getThumbnailSize(renderer, width, height);
    if (width <= 2 || height <= 2) return;
    ImageRender::Options options;
    options.cropToFill = true;
    options.useDisplayCache = true;
    renderer.rectangle.fill(0, 0, width - 2, height - 2, false);
    ImageRender::create(renderer, path).render(0, 0, width - 2, height - 2, options);
  }

  bool precacheExisting(const std::string& jpg, const std::string& png, const std::string& bmp) {
    if (!lockRenderer(pdMS_TO_TICKS(500))) return false;
    bool ready = false;
    if (SdMan.exists(jpg.c_str())) {
      precache(jpg);
      ready = true;
    } else if (SdMan.exists(png.c_str())) {
      precache(png);
      ready = true;
    } else if (SdMan.exists(bmp.c_str())) {
      precache(bmp);
      ready = true;
    }
    xSemaphoreGive(rendererMutex);
    return ready;
  }

  bool process(const std::string& path) {
    strlcpy(currentPath, path.c_str(), sizeof(currentPath));
    report();

    if (StringUtils::checkFileExtension(path, ".epub")) {
      Epub epub(path, "/.metadata/epub");
      const std::string jpg = epub.getThumbJpegPath();
      const std::string png = epub.getThumbPngPath();
      const std::string bmp = epub.getThumbBmpPath();
      if (SdMan.exists(jpg.c_str()) || SdMan.exists(png.c_str()) || SdMan.exists(bmp.c_str())) {
        ++result.skipped;
        ++result.processed;
        report();
        return true;
      }
      if (!epub.load()) {
        ++result.failed;
        ++result.processed;
        report();
        return true;
      }
      const bool ok = epub.generateThumbBmp();
      if (ok) {
        ++result.generated;
        precacheExisting(jpg, png, bmp);
      } else {
        ++result.failed;
      }
      ++result.processed;
      report();
      return true;
    }

    if (StringUtils::checkFileExtension(path, ".xtc")) {
      Xtc xtc(path, "/.metadata/xtc");
      const std::string bmp = xtc.getThumbBmpPath();
      if (SdMan.exists(bmp.c_str())) {
        ++result.skipped;
        ++result.processed;
        report();
        return true;
      }
      if (!xtc.load()) {
        ++result.failed;
        ++result.processed;
        report();
        return true;
      }
      const bool ok = xtc.generateThumbBmp();
      if (ok) {
        ++result.generated;
        if (lockRenderer(pdMS_TO_TICKS(500))) {
          precache(bmp);
          xSemaphoreGive(rendererMutex);
        }
      } else {
        ++result.failed;
      }
      ++result.processed;
      report();
      return true;
    }

    if (StringUtils::checkFileExtension(path, ".pdf")) {
      Pdf pdf(path, "/.metadata/pdf");
      const std::string bmp = pdf.getThumbBmpPath();
      if (SdMan.exists(bmp.c_str())) {
        ++result.skipped;
        ++result.processed;
        report();
        return true;
      }
      if (!pdf.load()) {
        ++result.failed;
        ++result.processed;
        report();
        return true;
      }
      bool ok = false;
      if (lockRenderer(pdMS_TO_TICKS(2000))) {
        ok = pdf.generateThumbBmp(renderer);
        if (ok) precache(bmp);
        xSemaphoreGive(rendererMutex);
      }
      if (ok) {
        ++result.generated;
      } else {
        ++result.failed;
      }
      ++result.processed;
      report();
      return true;
    }
    return true;
  }

  bool skipName(const char* name) const {
    return !name || name[0] == '.' || std::strcmp(name, "System Volume Information") == 0 ||
           std::strcmp(name, "sleep") == 0;
  }

  bool scan(const std::string& path) {
    if (cancelled()) return false;
    FsFile directory = SdMan.open(path.c_str());
    if (!directory || !directory.isDirectory()) {
      if (directory) directory.close();
      return true;
    }
    directory.rewindDirectory();
    char name[256] = {};
    while (!cancelled()) {
      FsFile entry = directory.openNextFile();
      if (!entry) break;
      entry.getName(name, sizeof(name));
      if (skipName(name)) {
        entry.close();
        continue;
      }
      const bool isDirectory = entry.isDirectory();
      entry.close();
      const std::string fullPath = (path == "/" ? path : path + "/") + name;
      if (isDirectory) {
        if (!scan(fullPath)) {
          directory.close();
          return false;
        }
      } else if (StringUtils::checkFileExtension(std::string(name), ".epub") ||
                 StringUtils::checkFileExtension(std::string(name), ".xtc") ||
                 StringUtils::checkFileExtension(std::string(name), ".pdf")) {
        if (!process(fullPath)) {
          directory.close();
          return false;
        }
        if ((result.processed & 3) == 0) vTaskDelay(pdMS_TO_TICKS(1));
      }
    }
    directory.close();
    return !cancelled();
  }
};
}  // namespace

bool ThumbnailGeneration::generate(GfxRenderer& renderer, SemaphoreHandle_t rendererMutex, Result& result,
                                   const std::function<void(int, const char*)>& progress,
                                   const std::function<bool()>& shouldCancel) {
  result = {};
  Generator generator{renderer, rendererMutex, result, progress, shouldCancel};
  return generator.scan("/");
}
