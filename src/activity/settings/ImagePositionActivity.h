#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "activity/Activity.h"

class ImagePositionActivity final : public Activity {
 public:
  using ApplyCallback = std::function<void(uint8_t, uint8_t)>;

  ImagePositionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string imagePath,
                        bool cropToFill, bool padding, int widgetX, int widgetY, int widgetWidth, int widgetHeight,
                        uint8_t cropAnchorX, uint8_t cropAnchorY,
                        ApplyCallback onApply, std::function<void()> onBack)
      : Activity("ImagePosition", renderer, mappedInput), imagePath_(std::move(imagePath)),
        cropToFill_(cropToFill), padding_(padding), widgetX_(widgetX), widgetY_(widgetY),
        widgetWidth_(widgetWidth), widgetHeight_(widgetHeight),
        cropAnchorX_(cropAnchorX), cropAnchorY_(cropAnchorY), onApply_(std::move(onApply)),
        onBack_(std::move(onBack)) {}

  ~ImagePositionActivity() override;

  void onEnter() override;
  void loop() override;
  bool skipLoopDelay() override { return dragging_; }

 private:
  std::string imagePath_;
  bool cropToFill_ = false;
  bool padding_ = false;
  int widgetX_ = 0;
  int widgetY_ = 0;
  int widgetWidth_ = 0;
  int widgetHeight_ = 0;
  uint8_t cropAnchorX_ = 128;
  uint8_t cropAnchorY_ = 128;
  bool dragging_ = false;
  int pressX_ = 0;
  int pressY_ = 0;
  uint8_t dragAnchorX_ = 128;
  uint8_t dragAnchorY_ = 128;
  int previewX_ = 0;
  int previewY_ = 0;
  int previewWidth_ = 0;
  int previewHeight_ = 0;
  int outerPreviewX_ = 0;
  int outerPreviewY_ = 0;
  int outerPreviewWidth_ = 0;
  int outerPreviewHeight_ = 0;
  uint8_t* rasterCache_ = nullptr;
  size_t rasterCacheBytes_ = 0;
  int rasterWidth_ = 0;
  int rasterHeight_ = 0;
  std::vector<uint8_t> renderRow_;
  ApplyCallback onApply_;
  std::function<void()> onBack_;

  void render();
  void updatePreviewBounds();
  bool prepareRasterCache();
  void releaseRasterCache();
  bool renderCachedImage(int x, int y, int width, int height);
  void close();
  void updateDrag(int x, int y);
};
