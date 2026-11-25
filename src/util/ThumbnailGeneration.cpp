#include "ThumbnailGeneration.h"

#include <Epub.h>
#include <GfxRenderer.h>
#include <ImageRender.h>
#include <Pdf.h>
#include <SDCardManager.h>
#include <Xtc.h>

#include <cstring>
#include <string>

#include "util/SdIoMutex.h"
#include "util/StringUtils.h"

namespace {
constexpr int kThumbnailMaxWidth = 360;
constexpr int kThumbnailMaxHeight = 540;

bool thumbnailExceedsTarget(const std::string& path) {
  int width = 0;
  int height = 0;
  if (!ImageRender::getDimensions(path, &width, &height)) return false;
  return width > kThumbnailMaxWidth || height > kThumbnailMaxHeight;
}

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

  bool process(const std::string& path) {
    strlcpy(currentPath, path.c_str(), sizeof(currentPath));
    report();

    if (StringUtils::checkFileExtension(path, ".epub")) {
      Epub epub(path, "/.metadata/epub");
      const std::string jpg = epub.getThumbJpegPath();
      const std::string png = epub.getThumbPngPath();
      const std::string bmp = epub.getThumbBmpPath();
      const bool hasJpg = SdMan.exists(jpg.c_str());
      const bool hasPng = SdMan.exists(png.c_str());
      const bool hasBmp = SdMan.exists(bmp.c_str());
      const bool hasThumbnail = hasJpg || hasPng || hasBmp;
      const bool oversizedJpg = hasJpg && thumbnailExceedsTarget(jpg);
      if (hasThumbnail && !oversizedJpg) {
        ++result.skipped;
        ++result.processed;
        report();
        return true;
      }
      if (oversizedJpg) {
        INX_SERIAL.printf("[%lu] [THUMB-GEN] replacing oversized thumbnail path=%s target=%dx%d\n", millis(),
                          jpg.c_str(), kThumbnailMaxWidth, kThumbnailMaxHeight);
        SdMan.remove(jpg.c_str());
        SdMan.remove(png.c_str());
        SdMan.remove(bmp.c_str());
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
      } else {
        ++result.failed;
      }
      ++result.processed;
      report();
      return true;
    }

    if (StringUtils::checkFileExtension(path, ".xtc")) {
      SdIoMutex::Lock ioLock;
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
      } else {
        ++result.failed;
      }
      ++result.processed;
      report();
      return true;
    }

    if (StringUtils::checkFileExtension(path, ".pdf")) {
      SdIoMutex::Lock ioLock;
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
      if (rendererMutex && xSemaphoreTake(rendererMutex, pdMS_TO_TICKS(2000)) == pdTRUE) {
        ok = pdf.generateThumbBmp(renderer);
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
}

bool ThumbnailGeneration::generate(GfxRenderer& renderer, SemaphoreHandle_t rendererMutex, Result& result,
                                   const std::function<void(int, const char*)>& progress,
                                   const std::function<bool()>& shouldCancel) {
  result = {};
  Generator generator{renderer, rendererMutex, result, progress, shouldCancel};
  return generator.scan("/");
}

bool ThumbnailGeneration::generateOne(GfxRenderer& renderer, SemaphoreHandle_t rendererMutex, Result& result,
                                      const char* path, const std::function<bool()>& shouldCancel) {
  if (!path || path[0] == '\0' || (shouldCancel && shouldCancel())) return false;
  const std::function<void(int, const char*)> noProgress;
  Generator generator{renderer, rendererMutex, result, noProgress, shouldCancel};
  return generator.process(path);
}
