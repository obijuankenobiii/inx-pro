#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <utility>

#include "activity/ActivityWithSubactivity.h"
#include "SettingsListLayout.h"

/** Settings screen for the Image home widget. */
class ImageSettingsActivity final : public ActivityWithSubactivity {
 public:
  using ApplyCallback = std::function<void(const std::string&, bool, bool, uint8_t, uint8_t)>;

  ImageSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string imagePath,
                        bool cropToFill, bool padding, uint8_t cropAnchorX, uint8_t cropAnchorY,
                        int widgetX, int widgetY, int widgetWidth, int widgetHeight,
                        ApplyCallback onApply, std::function<void()> onBack)
      : ActivityWithSubactivity("ImageSettings", renderer, mappedInput), imagePath_(std::move(imagePath)),
        cropToFill_(cropToFill), padding_(padding), cropAnchorX_(cropAnchorX), cropAnchorY_(cropAnchorY),
        widgetX_(widgetX), widgetY_(widgetY), widgetWidth_(widgetWidth), widgetHeight_(widgetHeight),
        onApply_(std::move(onApply)),
        onBack_(std::move(onBack)) {}

  void onEnter() override;
  void loop() override;

 private:
  static constexpr int kRowHeight = SettingsListLayout::kRowHeight;

  std::string imagePath_;
  bool cropToFill_ = false;
  bool padding_ = false;
  uint8_t cropAnchorX_ = 128;
  uint8_t cropAnchorY_ = 128;
  int widgetX_ = 0;
  int widgetY_ = 0;
  int widgetWidth_ = 0;
  int widgetHeight_ = 0;
  int contentTop_ = 0;
  ApplyCallback onApply_;
  std::function<void()> onBack_;

  void render();
  void chooseImage();
  void openPosition();
  void close();
  void handleTouch(int x, int y);
};
