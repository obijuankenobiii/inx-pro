#include "ImageSettingsActivity.h"

#include <GfxRenderer.h>

#include <algorithm>

#include "ImagePositionActivity.h"
#include "SleepImagePickerActivity.h"
#include "activity/page/SubPage.h"
#include "activity/page/components/global/Toggle.h"
#include "images/LibraryFilterRight.h"
#include "system/Fonts.h"

namespace {
bool inside(const int x, const int y, const int left, const int top, const int width, const int height) {
  return x >= left && x < left + width && y >= top && y < top + height;
}

const char* positionLabel(const uint8_t x, const uint8_t y) {
  return x == 128 && y == 128 ? "Center" : "Custom";
}
}

void ImageSettingsActivity::onEnter() { render(); }

void ImageSettingsActivity::render() {
  renderer.clearScreen();
  contentTop_ = SubPage::header(renderer, "Image");
  const int font = systemFontId();
  const int width = renderer.getScreenWidth();
  const int lineHeight = renderer.text.getLineHeight(font);

  renderer.rectangle.fill(0, contentTop_, width, kRowHeight * 4, static_cast<int>(GfxRenderer::FillTone::Paper));
  renderer.text.render(font, 20, contentTop_ + (kRowHeight - lineHeight) / 2, "Choose an image", true,
                       EpdFontFamily::REGULAR);
  constexpr int caretWidth = 32;
  constexpr int caretHeight = 30;
  renderer.bitmap.icon(LibraryFilterRight, width - 24 - caretWidth,
                       contentTop_ + (kRowHeight - caretHeight) / 2, caretWidth, caretHeight);

  const int toggleY = contentTop_ + kRowHeight;
  renderer.text.render(font, 20, toggleY + (kRowHeight - lineHeight) / 2, "Crop to fill", true,
                       EpdFontFamily::REGULAR);
  Toggle::render(renderer, width - 20, toggleY, kRowHeight, cropToFill_);
  renderer.line.render(SettingsListLayout::kDividerInset, toggleY - 1,
                       width - SettingsListLayout::kDividerInset, toggleY - 1, true,
                       LineRender::Style::Dotted);
  const int paddingY = contentTop_ + kRowHeight * 2;
  renderer.text.render(font, 20, paddingY + (kRowHeight - lineHeight) / 2, "Padding", true,
                       EpdFontFamily::REGULAR);
  Toggle::render(renderer, width - 20, paddingY, kRowHeight, padding_);
  renderer.line.render(SettingsListLayout::kDividerInset, paddingY - 1,
                       width - SettingsListLayout::kDividerInset, paddingY - 1, true,
                       LineRender::Style::Dotted);
  const int positionY = contentTop_ + kRowHeight * 3;
  renderer.text.render(font, 20, positionY + (kRowHeight - lineHeight) / 2, "Position", true,
                       EpdFontFamily::REGULAR);
  const char* position = positionLabel(cropAnchorX_, cropAnchorY_);
  renderer.text.render(font, width - renderer.text.getWidth(font, position) - 20,
                       positionY + (kRowHeight - lineHeight) / 2, position, true, EpdFontFamily::REGULAR);
  renderer.line.render(SettingsListLayout::kDividerInset, positionY - 1,
                       width - SettingsListLayout::kDividerInset, positionY - 1, true,
                       LineRender::Style::Dotted);
  renderer.displayBuffer();
}

void ImageSettingsActivity::chooseImage() {
  enterNewActivity(new SleepImagePickerActivity(
      renderer, mappedInput,
      [this] {
        exitActivity();
        render();
      },
      [this](const std::string& path) { imagePath_ = path; }, imagePath_, "Choose widget image", false));
}

void ImageSettingsActivity::openPosition() {
  enterNewActivity(new ImagePositionActivity(
      renderer, mappedInput, imagePath_, cropToFill_, padding_, widgetX_, widgetY_, widgetWidth_, widgetHeight_,
      cropAnchorX_, cropAnchorY_,
      [this](const uint8_t cropAnchorX, const uint8_t cropAnchorY) {
        cropAnchorX_ = cropAnchorX;
        cropAnchorY_ = cropAnchorY;
      },
      [this] {
        exitActivity();
        render();
      }));
}

void ImageSettingsActivity::close() {
  if (onApply_) onApply_(imagePath_, cropToFill_, padding_, cropAnchorX_, cropAnchorY_);
  if (onBack_) onBack_();
}

void ImageSettingsActivity::handleTouch(const int x, const int y) {
  if (!inside(x, y, 0, contentTop_, renderer.getScreenWidth(), kRowHeight * 4)) return;
  const int row = (y - contentTop_) / kRowHeight;
  if (row == 0) {
    chooseImage();
  } else if (row == 1) {
    cropToFill_ = !cropToFill_;
    render();
  } else if (row == 2) {
    padding_ = !padding_;
    render();
  } else if (row == 3) {
    openPosition();
  }
}

void ImageSettingsActivity::loop() {
  if (subActivity) {
    subActivity->loop();
    return;
  }
  if (SubPage::closeInput(renderer, mappedInput, [this] { close(); })) return;
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    close();
    return;
  }
  if (mappedInput.hasTouch()) {
    float nx = 0.0f;
    float ny = 0.0f;
    if (mappedInput.wasTouchTapInScreen(renderer, nx, ny)) {
      handleTouch(static_cast<int>(nx * renderer.getScreenWidth()),
                  static_cast<int>(ny * renderer.getScreenHeight()));
    }
  }
}
