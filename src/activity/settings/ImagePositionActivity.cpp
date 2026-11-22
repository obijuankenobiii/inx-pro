#include "ImagePositionActivity.h"

#include <BitmapRender.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <ImageRender.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cmath>

#include "activity/page/SubPage.h"
#include "state/SystemSetting.h"
#include "system/Fonts.h"

namespace {
int insetFor(const bool padding, const int size) {
  return padding ? std::min(20, size / 2) : 0;
}

uint8_t anchorFromDrag(const uint8_t start, const int delta, const int range) {
  if (range <= 0) return start;
  const int value = static_cast<int>(start) - delta * 255 / range;
  return static_cast<uint8_t>(std::max(0, std::min(255, value)));
}

BitmapRender::RoundedOutside roundedOutside() {
  if (SETTINGS.bitmapRoundedCorners == 1) return BitmapRender::RoundedOutside::PaperOutside;
  if (SETTINGS.bitmapRoundedCorners == 2) return BitmapRender::RoundedOutside::SubtlePaperOutside;
  return BitmapRender::RoundedOutside::None;
}
}

void ImagePositionActivity::onEnter() {
  updatePreviewBounds();
  prepareRasterCache();
  render();
}

ImagePositionActivity::~ImagePositionActivity() { releaseRasterCache(); }

void ImagePositionActivity::releaseRasterCache() {
  if (rasterCache_) heap_caps_free(rasterCache_);
  rasterCache_ = nullptr;
  rasterCacheBytes_ = 0;
  rasterWidth_ = 0;
  rasterHeight_ = 0;
  renderRow_.clear();
}

bool ImagePositionActivity::prepareRasterCache() {
  releaseRasterCache();
  if (imagePath_.empty()) return false;

  int sourceWidth = 0;
  int sourceHeight = 0;
  if (!ImageRender::getDimensions(imagePath_, &sourceWidth, &sourceHeight) || sourceWidth <= 0 || sourceHeight <= 0) {
    return false;
  }

  const int maxWidth = std::max(1, renderer.getScreenWidth());
  const int maxHeight = std::max(1, renderer.getScreenHeight());
  const float scale = std::min(static_cast<float>(maxWidth) / sourceWidth,
                               static_cast<float>(maxHeight) / sourceHeight);
  rasterWidth_ = std::max(1, static_cast<int>(std::lround(sourceWidth * scale)));
  rasterHeight_ = std::max(1, static_cast<int>(std::lround(sourceHeight * scale)));
  const size_t rowBytes = (static_cast<size_t>(rasterWidth_) + 7u) / 8u;
  rasterCacheBytes_ = rowBytes * static_cast<size_t>(rasterHeight_);
  rasterCache_ = static_cast<uint8_t*>(heap_caps_malloc(rasterCacheBytes_, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!rasterCache_) rasterCache_ = static_cast<uint8_t*>(heap_caps_malloc(rasterCacheBytes_, MALLOC_CAP_8BIT));
  if (!rasterCache_) {
    releaseRasterCache();
    return false;
  }

  renderer.syncWriteBufferFromActive();
  renderer.clearScreen();
  ImageRender::Options options;
  options.mode = ImageRenderMode::OneBit;
  options.cropToFill = false;
  options.useDisplayCache = true;
  options.asyncDisplayCache = false;
  options.cacheNamespace = ImageDisplayCacheNamespace::Widgets;
  if (!ImageRender::create(renderer, imagePath_).render(0, 0, rasterWidth_, rasterHeight_, options)) {
    releaseRasterCache();
    return false;
  }

  for (int row = 0; row < rasterHeight_; ++row) {
    if (!renderer.readPackedRow1bpp(0, row, rasterWidth_, rasterCache_ + row * rowBytes)) {
      releaseRasterCache();
      return false;
    }
  }

  const int imageWidth = previewWidth_;
  const int imageHeight = previewHeight_;
  const float displayScale = cropToFill_
                                 ? std::max(static_cast<float>(imageWidth) / rasterWidth_,
                                            static_cast<float>(imageHeight) / rasterHeight_)
                                 : std::min(static_cast<float>(imageWidth) / rasterWidth_,
                                            static_cast<float>(imageHeight) / rasterHeight_);
  const int displayWidth = std::max(1, static_cast<int>(std::lround(rasterWidth_ * displayScale)));
  const int displayHeight = std::max(1, static_cast<int>(std::lround(rasterHeight_ * displayScale)));
  const size_t sourceRowBytes = (static_cast<size_t>(rasterWidth_) + 7u) / 8u;
  const size_t displayRowBytes = (static_cast<size_t>(displayWidth) + 7u) / 8u;
  const size_t displayBytes = displayRowBytes * static_cast<size_t>(displayHeight);
  uint8_t* displayCache = static_cast<uint8_t*>(heap_caps_malloc(displayBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!displayCache) displayCache = static_cast<uint8_t*>(heap_caps_malloc(displayBytes, MALLOC_CAP_8BIT));
  if (!displayCache) return true;

  for (int row = 0; row < displayHeight; ++row) {
    uint8_t* targetRow = displayCache + static_cast<size_t>(row) * displayRowBytes;
    std::fill(targetRow, targetRow + displayRowBytes, 0);
    const int sourceY = std::min(rasterHeight_ - 1, row * rasterHeight_ / displayHeight);
    const uint8_t* sourceRow = rasterCache_ + static_cast<size_t>(sourceY) * sourceRowBytes;
    for (int column = 0; column < displayWidth; ++column) {
      const int sourceX = std::min(rasterWidth_ - 1, column * rasterWidth_ / displayWidth);
      if ((sourceRow[sourceX / 8] & (0x80u >> (sourceX % 8))) != 0) {
        targetRow[static_cast<size_t>(column) / 8] |= static_cast<uint8_t>(0x80u >> (column % 8));
      }
    }
  }
  heap_caps_free(rasterCache_);
  rasterCache_ = displayCache;
  rasterCacheBytes_ = displayBytes;
  rasterWidth_ = displayWidth;
  rasterHeight_ = displayHeight;
  return true;
}

void ImagePositionActivity::updatePreviewBounds() {
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  const int font = systemFontIdForPointSize(12);
  const int lineHeight = renderer.text.getLineHeight(font);
  if (widgetWidth_ > 0 && widgetHeight_ > 0) {
    const int insetX = insetFor(padding_, widgetWidth_);
    const int insetY = insetFor(padding_, widgetHeight_);
    previewX_ = widgetX_ + insetX;
    previewY_ = widgetY_ + insetY;
    previewWidth_ = std::max(1, widgetWidth_ - insetX * 2);
    previewHeight_ = std::max(1, widgetHeight_ - insetY * 2);
  } else {
    const int previewY = navigation::Menu::topPadding + navigation::Menu::iconSize + 20;
    const int outerWidth = std::max(1, width);
    const int outerHeight = std::max(1, height - previewY - lineHeight - 10);
    const int insetX = insetFor(padding_, outerWidth);
    const int insetY = insetFor(padding_, outerHeight);
    previewX_ = insetX;
    previewY_ = previewY + insetY;
    previewWidth_ = std::max(1, outerWidth - insetX * 2);
    previewHeight_ = std::max(1, outerHeight - insetY * 2);
  }
}

bool ImagePositionActivity::renderCachedImage(const int x, const int y, const int width, const int height) {
  if (!rasterCache_ || rasterWidth_ <= 0 || rasterHeight_ <= 0 || width <= 0 || height <= 0) return false;

  const int outputWidth = cropToFill_ ? width : rasterWidth_;
  const int outputHeight = cropToFill_ ? height : rasterHeight_;
  const int outputX = x + (width - outputWidth) / 2;
  const int outputY = y + (height - outputHeight) / 2;
  const int cropWidth = std::max(0, rasterWidth_ - outputWidth);
  const int cropHeight = std::max(0, rasterHeight_ - outputHeight);
  const int cropX = static_cast<int>(std::lround(cropWidth * (cropAnchorX_ / 255.0f)));
  const int cropY = static_cast<int>(std::lround(cropHeight * (cropAnchorY_ / 255.0f)));
  const size_t sourceRowBytes = (static_cast<size_t>(rasterWidth_) + 7u) / 8u;
  renderRow_.resize((static_cast<size_t>(outputWidth) + 7u) / 8u);
  const bool wasImageRendering = renderer.isImageRendering();
  renderer.setImageRendering(true);
  for (int row = 0; row < outputHeight; ++row) {
    const int sourceY = std::min(rasterHeight_ - 1, cropY + row);
    const uint8_t* sourceRow = rasterCache_ + static_cast<size_t>(sourceY) * sourceRowBytes;
    const int sourceByte = cropX / 8;
    const int sourceShift = cropX % 8;
    for (size_t byte = 0; byte < renderRow_.size(); ++byte) {
      const size_t nextByte = static_cast<size_t>(sourceByte) + byte;
      const uint16_t pair = static_cast<uint16_t>(sourceRow[nextByte]) << 8 |
                            (nextByte + 1 < sourceRowBytes ? sourceRow[nextByte + 1] : 0);
      renderRow_[byte] = sourceShift == 0 ? static_cast<uint8_t>(pair >> 8)
                                          : static_cast<uint8_t>(pair >> (8 - sourceShift));
    }
    if (outputWidth % 8 != 0 && !renderRow_.empty()) {
      renderRow_.back() &= static_cast<uint8_t>(0xFFu << (8 - outputWidth % 8));
    }
    renderer.drawPackedRow1bpp(outputX, outputY + row, outputWidth, renderRow_.data());
  }
  renderer.setImageRendering(wasImageRendering);
  const BitmapRender::RoundedOutside corners = roundedOutside();
  if (corners != BitmapRender::RoundedOutside::None) {
    renderer.bitmap.maskRoundedOutside(x, y, width, height, corners);
  }
  return true;
}

void ImagePositionActivity::render() {
  renderer.syncWriteBufferFromActive();
  renderer.clearScreen();
  const int contentTop = SubPage::header(renderer, "Position image");
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  const int font = systemFontIdForPointSize(12);
  const int lineHeight = renderer.text.getLineHeight(font);
  updatePreviewBounds();

  if (widgetWidth_ > 0 && widgetHeight_ > 0) {
    const bool rounded = SETTINGS.bitmapRoundedCorners != 0;
    renderer.rectangle.fill(widgetX_, widgetY_, widgetWidth_, widgetHeight_, false, rounded,
                            SETTINGS.bitmapRoundedCorners == 2);
  }
  const int imageX = previewX_;
  const int imageY = previewY_;
  const int imageWidth = previewWidth_;
  const int imageHeight = previewHeight_;

  bool rendered = renderCachedImage(imageX, imageY, imageWidth, imageHeight);
  if (!rendered && !imagePath_.empty()) {
    ImageRender::Options options;
    options.mode = ImageRenderMode::OneBit;
    options.cropToFill = cropToFill_;
    options.cropAnchorX = cropAnchorX_ / 255.0f;
    options.cropAnchorY = cropAnchorY_ / 255.0f;
    options.roundedOutside = roundedOutside();
    options.useDisplayCache = true;
    options.asyncDisplayCache = true;
    options.cacheNamespace = ImageDisplayCacheNamespace::Widgets;
    rendered = ImageRender::create(renderer, imagePath_).render(imageX, imageY, imageWidth, imageHeight, options);
  }
  if (!rendered) {
    renderer.rectangle.render(imageX, imageY, imageWidth, imageHeight, true);
    const char* label = imagePath_.empty() ? "Choose image first" : "Image unavailable";
    const int labelWidth = renderer.text.getWidth(font, label);
    renderer.text.render(font, imageX + std::max(0, (imageWidth - labelWidth) / 2),
                         imageY + std::max(0, (imageHeight - lineHeight) / 2), label, true,
                         EpdFontFamily::REGULAR);
  }

  if (previewY_ + previewHeight_ + lineHeight + 10 <= height) {
    const char* instruction = cropToFill_ ? "Drag image to position" : "Turn on Crop to fill to position";
    const std::string shownInstruction = renderer.text.truncate(font, instruction, std::max(1, width - 40));
    const int instructionWidth = renderer.text.getWidth(font, shownInstruction.c_str());
    renderer.text.render(font, std::max(20, (width - instructionWidth) / 2),
                         height - lineHeight - 10, shownInstruction.c_str(), true, EpdFontFamily::REGULAR);
  }
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  renderer.syncWriteBufferFromActive();
}

void ImagePositionActivity::close() {
  if (onApply_) onApply_(cropAnchorX_, cropAnchorY_);
  if (onBack_) onBack_();
}

void ImagePositionActivity::updateDrag(const int x, const int y) {
  const uint8_t nextAnchorX = anchorFromDrag(dragAnchorX_, x - pressX_, previewWidth_);
  const uint8_t nextAnchorY = anchorFromDrag(dragAnchorY_, y - pressY_, previewHeight_);
  if (nextAnchorX == cropAnchorX_ && nextAnchorY == cropAnchorY_) return;
  cropAnchorX_ = nextAnchorX;
  cropAnchorY_ = nextAnchorY;
  render();
}

void ImagePositionActivity::loop() {
  if (dragging_) {
    if (mappedInput.isTouchPressed()) {
      float nx = 0.0f;
      float ny = 0.0f;
      if (mappedInput.isTouchHeldInScreen(renderer, nx, ny)) {
        updateDrag(static_cast<int>(nx * renderer.getScreenWidth()),
                   static_cast<int>(ny * renderer.getScreenHeight()));
      }
      return;
    }
    dragging_ = false;
    return;
  }

  float nx = 0.0f;
  float ny = 0.0f;
  if (mappedInput.wasTouchPressedInScreen(renderer, nx, ny)) {
    const int x = static_cast<int>(nx * renderer.getScreenWidth());
    const int y = static_cast<int>(ny * renderer.getScreenHeight());
    if (x >= previewX_ && x < previewX_ + previewWidth_ && y >= previewY_ && y < previewY_ + previewHeight_) {
      dragging_ = true;
      pressX_ = x;
      pressY_ = y;
      dragAnchorX_ = cropAnchorX_;
      dragAnchorY_ = cropAnchorY_;
      return;
    }
  }

  if (SubPage::closeInput(renderer, mappedInput, [this] { close(); })) return;
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    close();
    return;
  }
}
