#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

class EpubActivity;
class GfxRenderer;

/** Full-screen reader image view opened by long-pressing an inline EPUB image. */
class EpubImageViewerUi {
 public:
  bool isActive() const { return active; }

  void open(EpubActivity& activity, const std::string& imagePath, int pageImageX = -1, int pageImageY = -1,
            int pageImageWidth = 0, int pageImageHeight = 0);
  void close(EpubActivity& activity);
  void handleInput(EpubActivity& activity);

 private:
  void render(EpubActivity& activity);
  void zoom(EpubActivity& activity);
  void rotate(EpubActivity& activity);
  void pan(EpubActivity& activity, int x, int y);
  void panForSwipe(EpubActivity& activity, int x, int y);
  void clampPan(int screenWidth, int screenHeight);
  void releaseRasterCache();
  bool captureRasterCache(const GfxRenderer& renderer, int x, int y);
  bool renderCachedZoom(EpubActivity& activity, int x, int y, int width, int height) const;
  void buttons(EpubActivity& activity) const;
  bool closeAt(EpubActivity& activity, int x, int y) const;
  bool rotateAt(EpubActivity& activity, int x, int y) const;
  void mapTouch(EpubActivity& activity, float nx, float ny, int& x, int& y) const;
  void restoreOrientation(EpubActivity& activity) const;

  bool active = false;
  std::string path;
  int sourceWidth = 0;
  int sourceHeight = 0;
  int fitWidth = 0;
  int fitHeight = 0;
  int fitX = 0;
  int fitY = 0;
  int zoomLevel = 1;
  int panX = 0;
  int panY = 0;
  int originalOrientation = -1;
  int pageImageX = -1;
  int pageImageY = -1;
  int pageImageWidth = 0;
  int pageImageHeight = 0;
  int rasterWidth = 0;
  int rasterHeight = 0;
  uint8_t* rasterCache = nullptr;
  size_t rasterCacheBytes = 0;
  bool cleanRefreshRequired = false;
};
