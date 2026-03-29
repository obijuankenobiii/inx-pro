#include "EpubImageViewerUi.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <ImageRender.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cstring>

#include "EpubActivity.h"
#include "images/Close.h"
#include "images/Refresh.h"
#include "state/ReaderSetting.h"
#include "state/SystemSetting.h"
#include "system/MappedInputManager.h"
#include "util/StringUtils.h"

namespace {
constexpr int closeSize = 40;
constexpr int closeMargin = 20;
constexpr int closeHitPadding = 6;
constexpr int buttonGap = 8;

int closeX(const GfxRenderer& renderer) {
  return renderer.getScreenWidth() - closeMargin - closeSize;
}

int rotateX(const GfxRenderer& renderer) {
  return closeX(renderer) - buttonGap - closeSize;
}
}  // namespace

void EpubImageViewerUi::open(EpubActivity& activity, const std::string& imagePath, const int pageImageXIn,
                             const int pageImageYIn, const int pageImageWidthIn, const int pageImageHeightIn) {
  if (imagePath.empty()) {
    return;
  }

  releaseRasterCache();

  pageImageX = pageImageXIn;
  pageImageY = pageImageYIn;
  pageImageWidth = pageImageWidthIn;
  pageImageHeight = pageImageHeightIn;

  if (pageImageWidth > 0 && pageImageHeight > 0) {
    // PageImage already supplied the dimensions of the rendered raster. Do not reopen the PNG.
    sourceWidth = pageImageWidth;
    sourceHeight = pageImageHeight;
  } else if (!ImageRender::getDimensions(imagePath, &sourceWidth, &sourceHeight) || sourceWidth <= 0 ||
             sourceHeight <= 0) {
    activity.readerPopup("Could not open image");
    return;
  }

  path = imagePath;
  originalOrientation = static_cast<int>(activity.renderer.getOrientation());
  zoomLevel = 1;
  panX = 0;
  panY = 0;
  cleanRefreshRequired = false;
  active = true;
  render(activity);
}

void EpubImageViewerUi::close(EpubActivity& activity) {
  restoreOrientation(activity);
  releaseRasterCache();
  active = false;
  path.clear();
  sourceWidth = 0;
  sourceHeight = 0;
  fitWidth = 0;
  fitHeight = 0;
  fitX = 0;
  fitY = 0;
  zoomLevel = 1;
  panX = 0;
  panY = 0;
  originalOrientation = -1;
  pageImageX = -1;
  pageImageY = -1;
  pageImageWidth = 0;
  pageImageHeight = 0;
  rasterWidth = 0;
  rasterHeight = 0;
  cleanRefreshRequired = false;
  activity.updateRequired = true;
  activity.startPageTimer();
}

void EpubImageViewerUi::handleInput(EpubActivity& activity) {
  if (!active) {
    return;
  }

  const MappedInputManager& input = activity.mappedInput;
  // Process the direction in reader coordinates first. A rotated vertical
  // pan can begin at a native horizontal edge, which the HAL also marks as
  // Back for non-reader screens.
  if (input.wasTouchSwipeLeftForRenderer(activity.renderer)) {
    panForSwipe(activity, -1, 0);
    return;
  }
  if (input.wasTouchSwipeRightForRenderer(activity.renderer)) {
    panForSwipe(activity, 1, 0);
    return;
  }
  if (input.wasTouchSwipeUpForRenderer(activity.renderer)) {
    close(activity);
    return;
  }
  if (input.wasTouchSwipeDownForRenderer(activity.renderer)) {
    panForSwipe(activity, 0, 1);
    return;
  }

  if (input.wasReleased(MappedInputManager::Button::Back) || input.wasReleased(MappedInputManager::Button::Confirm)) {
    close(activity);
    return;
  }

  float tapX = 0.0f;
  float tapY = 0.0f;
  if (input.hasTouch() && input.wasTouchTapInScreen(activity.renderer, tapX, tapY)) {
    int x = 0;
    int y = 0;
    mapTouch(activity, tapX, tapY, x, y);
    if (closeAt(activity, x, y)) {
      close(activity);
      return;
    }
    if (rotateAt(activity, x, y)) {
      rotate(activity);
      return;
    }
    zoom(activity);
  }
}

void EpubImageViewerUi::render(EpubActivity& activity) {
  if (sourceWidth <= 0 || sourceHeight <= 0) {
    active = false;
    path.clear();
    activity.readerPopup("Could not open image");
    return;
  }

  const int screenWidth = activity.renderer.getScreenWidth();
  const int screenHeight = activity.renderer.getScreenHeight();
  const bool isPng = StringUtils::checkFileExtension(path, ".png");
  constexpr int margin = 12;
  const int availableWidth = std::max(1, screenWidth - margin * 2);
  const int availableHeight = std::max(1, screenHeight - margin * 2);
  const float scale = std::min(static_cast<float>(availableWidth) / sourceWidth,
                               static_cast<float>(availableHeight) / sourceHeight);
  fitWidth = std::max(1, static_cast<int>(sourceWidth * scale));
  fitHeight = std::max(1, static_cast<int>(sourceHeight * scale));
  const int width = fitWidth * zoomLevel;
  const int height = fitHeight * zoomLevel;
  clampPan(screenWidth, screenHeight);
  const int x = (screenWidth - width) / 2 + panX;
  const int y = (screenHeight - height) / 2 + panY;

  // PNGs are already reduced to one bit. Keep the viewer on the ordinary BW
  // framebuffer path so a stale grayscale render mode cannot turn a zoom/pan
  // redraw into a second image plane.
  if (isPng) {
    activity.renderer.setRenderMode(GfxRenderer::BW);
  }

  const auto present = [&] {
    buttons(activity);
#if FREEINK_DEVICE_X4PRO
    // A fast differential update is fine for the initial image, but after a
    // PNG has moved the old image is no longer the same geometry. A full
    // refresh is required here to remove the previous zoom/pan image instead
    // of allowing its charge to remain as visible ghosting.
    const HalDisplay::RefreshMode mode = isPng && cleanRefreshRequired ? HalDisplay::FULL_REFRESH
                                                                         : HalDisplay::FAST_REFRESH;
#else
    const HalDisplay::RefreshMode mode = HalDisplay::FAST_REFRESH;
#endif
    activity.renderer.displayBuffer(mode);
    cleanRefreshRequired = false;
  };

  bool pageRasterReady = false;
  if (zoomLevel == 1 && !rasterCache && pageImageX >= 0 && pageImageY >= 0 && pageImageWidth == fitWidth &&
      pageImageHeight == fitHeight) {
    // The page is still in the framebuffer here. Capture it before the viewer clears the screen.
    pageRasterReady = captureRasterCache(activity.renderer, pageImageX, pageImageY);
  }

  activity.renderer.clearScreen(0xFF);

  ImageRender::Options options;
  options.mode = READER_SETTINGS.readerImageGrayscale != 0 ? ImageRenderMode::TwoBit : ImageRenderMode::OneBit;
  options.quality = READER_SETTINGS.readerImageGrayscale == SystemSetting::READER_IMAGE_HIGH;
  options.fastQuality = false;
  options.useDisplayCache = true;
  options.asyncDisplayCache = true;

  // Normal image viewing is one-bit. Once the fitted raster is captured in PSRAM,
  // zoom/pan can redraw it without reopening or decoding the source image.
  if (options.mode == ImageRenderMode::OneBit && rasterCache && fitWidth > 0 && fitHeight > 0 &&
      renderCachedZoom(activity, x, y, width, height)) {
    present();
    return;
  }

  // Grayscale rendering owns temporary controller buffers; BW rendering does not
  // need to tear those buffers down on every pan.
  if (options.mode == ImageRenderMode::TwoBit) {
    activity.renderer.resetTransientReaderState();
  }

  if (options.mode == ImageRenderMode::OneBit && pageRasterReady) {
    present();
    return;
  }

  const ImageRender image = ImageRender::create(activity.renderer, path);
  if (options.mode == ImageRenderMode::TwoBit) {
    image.displayGrayscale(x, y, width, height, options, options.quality, [this, &activity] { buttons(activity); });
  } else if (image.render(x, y, width, height, options)) {
    fitX = (screenWidth - fitWidth) / 2;
    fitY = (screenHeight - fitHeight) / 2;
    if (zoomLevel == 1) {
      captureRasterCache(activity.renderer, fitX, fitY);
    }
    present();
  } else {
    restoreOrientation(activity);
    active = false;
    path.clear();
    activity.readerPopup("Could not open image");
  }
}

void EpubImageViewerUi::releaseRasterCache() {
  if (rasterCache) {
    heap_caps_free(rasterCache);
    rasterCache = nullptr;
  }
  rasterCacheBytes = 0;
  rasterWidth = 0;
  rasterHeight = 0;
}

bool EpubImageViewerUi::captureRasterCache(const GfxRenderer& renderer, const int x, const int y) {
  if (fitWidth <= 0 || fitHeight <= 0) {
    releaseRasterCache();
    return false;
  }

  const size_t rowBytes = (static_cast<size_t>(fitWidth) + 7u) / 8u;
  const size_t bytes = rowBytes * static_cast<size_t>(fitHeight);
  if (bytes == 0) {
    releaseRasterCache();
    return false;
  }

  if (!rasterCache || rasterCacheBytes != bytes) {
    releaseRasterCache();
    rasterCache = static_cast<uint8_t*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!rasterCache) {
      rasterCache = static_cast<uint8_t*>(heap_caps_malloc(bytes, MALLOC_CAP_8BIT));
    }
    if (!rasterCache) {
      return false;
    }
    rasterCacheBytes = bytes;
  }

  rasterWidth = fitWidth;
  rasterHeight = fitHeight;

  for (int row = 0; row < fitHeight; ++row) {
    if (!renderer.readPackedRow1bpp(x, y + row, fitWidth, rasterCache + row * rowBytes)) {
      releaseRasterCache();
      return false;
    }
  }
  return true;
}

bool EpubImageViewerUi::renderCachedZoom(EpubActivity& activity, const int x, const int y, const int width,
                                         const int height) const {
  if (!rasterCache || rasterWidth <= 0 || rasterHeight <= 0 || fitWidth <= 0 || fitHeight <= 0 || width <= 0 ||
      height <= 0) {
    return false;
  }

  const size_t rowBytes = (static_cast<size_t>(rasterWidth) + 7u) / 8u;
  const int screenWidth = activity.renderer.getScreenWidth();
  const int screenHeight = activity.renderer.getScreenHeight();
  const int destStartX = std::max(0, (-x + zoomLevel - 1) / zoomLevel);
  const int destEndX = std::min(fitWidth, std::max(0, (screenWidth - x + zoomLevel - 1) / zoomLevel));
  const int destStartY = std::max(0, (-y + zoomLevel - 1) / zoomLevel);
  const int destEndY = std::min(fitHeight, std::max(0, (screenHeight - y + zoomLevel - 1) / zoomLevel));
  for (int destY = destStartY; destY < destEndY; ++destY) {
    const int sourceY = std::min(rasterHeight - 1, (destY * rasterHeight) / fitHeight);
    const uint8_t* row = rasterCache + static_cast<size_t>(sourceY) * rowBytes;
    for (int destX = destStartX; destX < destEndX; ++destX) {
      const int sourceX = std::min(rasterWidth - 1, (destX * rasterWidth) / fitWidth);
      if ((row[sourceX / 8] & (0x80u >> (sourceX % 8))) == 0) {
        continue;
      }

      const int pixelX = x + destX * zoomLevel;
      const int pixelY = y + destY * zoomLevel;
      for (int dy = 0; dy < zoomLevel; ++dy) {
        const int screenY = pixelY + dy;
        if (screenY < 0 || screenY >= screenHeight) {
          continue;
        }
        for (int dx = 0; dx < zoomLevel; ++dx) {
          const int screenX = pixelX + dx;
          if (screenX >= 0 && screenX < screenWidth) {
            activity.renderer.drawPixel(screenX, screenY, true);
          }
        }
      }
    }
  }
  return true;
}

void EpubImageViewerUi::zoom(EpubActivity& activity) {
  // A second tap returns to the fitted view; this keeps the viewer control-free and touch-only.
  zoomLevel = zoomLevel == 1 ? 2 : 1;
  panX = 0;
  panY = 0;
  if (StringUtils::checkFileExtension(path, ".png")) {
    cleanRefreshRequired = true;
  }
  render(activity);
}

void EpubImageViewerUi::rotate(EpubActivity& activity) {
  const GfxRenderer::Orientation current = activity.renderer.getOrientation();
  const bool isLandscape = current == GfxRenderer::LandscapeClockwise ||
                           current == GfxRenderer::LandscapeCounterClockwise;
  const GfxRenderer::Orientation orientation =
      isLandscape ? GfxRenderer::Portrait : GfxRenderer::LandscapeCounterClockwise;

  activity.renderer.setOrientation(orientation);
  activity.mappedInput.setInvertDirectionalAxes180(orientation == GfxRenderer::LandscapeClockwise);
  zoomLevel = 1;
  panX = 0;
  panY = 0;
  if (StringUtils::checkFileExtension(path, ".png")) {
    cleanRefreshRequired = true;
  }
  render(activity);
}

void EpubImageViewerUi::pan(EpubActivity& activity, const int x, const int y) {
  if (zoomLevel == 1) {
    return;
  }

  const int step = std::max(48, std::min(activity.renderer.getScreenWidth(), activity.renderer.getScreenHeight()) / 3);
  panX += x * step;
  panY += y * step;
  if (StringUtils::checkFileExtension(path, ".png")) {
    cleanRefreshRequired = true;
  }
  render(activity);
}

void EpubImageViewerUi::panForSwipe(EpubActivity& activity, const int x, const int y) {
  int panStepX = x;
  int panStepY = y;
  switch (activity.renderer.getOrientation()) {
    case GfxRenderer::Portrait:
      break;
    case GfxRenderer::LandscapeClockwise:
      panStepX = -y;
      panStepY = x;
      break;
    case GfxRenderer::PortraitInverted:
      panStepX = -x;
      panStepY = -y;
      break;
    case GfxRenderer::LandscapeCounterClockwise:
      panStepX = y;
      panStepY = -x;
      break;
  }
  pan(activity, panStepX, panStepY);
}

void EpubImageViewerUi::clampPan(const int screenWidth, const int screenHeight) {
  const int imageWidth = fitWidth * zoomLevel;
  const int imageHeight = fitHeight * zoomLevel;
  const int maxX = std::max(0, (imageWidth - screenWidth) / 2);
  const int maxY = std::max(0, (imageHeight - screenHeight) / 2);
  panX = std::clamp(panX, -maxX, maxX);
  panY = std::clamp(panY, -maxY, maxY);
}

void EpubImageViewerUi::buttons(EpubActivity& activity) const {
  GfxRenderer& renderer = activity.renderer;
  const int close = closeX(renderer);
  const int rotate = rotateX(renderer);
  renderer.rectangle.fill(rotate - closeHitPadding, closeMargin - closeHitPadding,
                          closeSize * 2 + buttonGap + closeHitPadding * 2,
                          closeSize + closeHitPadding * 2, false);
  renderer.bitmap.icon(Refresh, rotate, closeMargin, closeSize, closeSize);
  renderer.bitmap.icon(Close, close, closeMargin, closeSize, closeSize);
}

bool EpubImageViewerUi::closeAt(EpubActivity& activity, const int tapX, const int tapY) const {
  const int x = closeX(activity.renderer) - closeHitPadding;
  const int y = closeMargin - closeHitPadding;
  const int size = closeSize + closeHitPadding * 2;
  return tapX >= x && tapX < x + size && tapY >= y && tapY < y + size;
}

bool EpubImageViewerUi::rotateAt(EpubActivity& activity, const int tapX, const int tapY) const {
  const int x = rotateX(activity.renderer) - closeHitPadding;
  const int y = closeMargin - closeHitPadding;
  const int size = closeSize + closeHitPadding * 2;
  return tapX >= x && tapX < x + size && tapY >= y && tapY < y + size;
}

void EpubImageViewerUi::mapTouch(EpubActivity& activity, const float nx, const float ny, int& x, int& y) const {
  const int rawX = std::clamp(static_cast<int>(nx * HalDisplay::DISPLAY_HEIGHT), 0,
                              static_cast<int>(HalDisplay::DISPLAY_HEIGHT) - 1);
  const int rawY = std::clamp(static_cast<int>(ny * HalDisplay::DISPLAY_WIDTH), 0,
                              static_cast<int>(HalDisplay::DISPLAY_WIDTH) - 1);
  switch (activity.renderer.getOrientation()) {
    case GfxRenderer::Portrait:
      x = rawX;
      y = rawY;
      break;
    case GfxRenderer::LandscapeClockwise:
      x = HalDisplay::DISPLAY_WIDTH - 1 - rawY;
      y = rawX;
      break;
    case GfxRenderer::PortraitInverted:
      x = HalDisplay::DISPLAY_HEIGHT - 1 - rawX;
      y = HalDisplay::DISPLAY_WIDTH - 1 - rawY;
      break;
    case GfxRenderer::LandscapeCounterClockwise:
      x = rawY;
      y = HalDisplay::DISPLAY_HEIGHT - 1 - rawX;
      break;
  }
}

void EpubImageViewerUi::restoreOrientation(EpubActivity& activity) const {
  if (originalOrientation < 0) {
    return;
  }
  const auto orientation = static_cast<GfxRenderer::Orientation>(originalOrientation);
  activity.renderer.setOrientation(orientation);
  activity.mappedInput.setInvertDirectionalAxes180(orientation == GfxRenderer::LandscapeClockwise);
}
