#include "BaseLibraryActivity.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <string>

#include "activity/page/SubPage.h"
#include "activity/page/components/global/PopUp.h"
#include "activity/page/components/global/Toggle.h"
#include "system/Fonts.h"

namespace {
bool inside(const int x, const int y, const int left, const int top, const int width, const int height) {
  return x >= left && x < left + width && y >= top && y < top + height;
}
}

void BaseLibraryActivity::onEnter() {
  availableFolders_ = LibraryWidget::folders();
  folderSelected_.fill(0);
  for (int slot = 0; slot < LibraryWidget::kVisibleFolderCount; ++slot) {
    for (int i = 0; i < static_cast<int>(availableFolders_.size()); ++i) {
      if (availableFolders_[static_cast<size_t>(i)].path == selectedFolders_[static_cast<size_t>(slot)]) {
        folderSelected_[static_cast<size_t>(slot)] = i;
        break;
      }
    }
  }
  render();
}

void BaseLibraryActivity::render() {
  renderer.clearScreen();
  const int contentTop = SubPage::header(renderer, "Library");
  const int font = systemFontId();
  const int width = renderer.getScreenWidth();
  for (int slot = 0; slot < LibraryWidget::kVisibleFolderCount; ++slot) {
    const int folderY = contentTop + slot * kRowHeight;
    const std::string rowLabel = "Folder " + std::to_string(slot + 1);
    const std::string folderLabel = availableFolders_.empty()
                                        ? "Library"
                                        : availableFolders_[static_cast<size_t>(folderSelected_[static_cast<size_t>(slot)])].name;
    renderer.rectangle.fill(0, folderY, width, kRowHeight, static_cast<int>(GfxRenderer::FillTone::Paper));
    renderer.text.render(font, 20, folderY + (kRowHeight - renderer.text.getLineHeight(font)) / 2, rowLabel.c_str(),
                         true, EpdFontFamily::REGULAR);
    const int folderWidth = renderer.text.getWidth(font, folderLabel.c_str());
    renderer.text.render(font, width - folderWidth - 20,
                         folderY + (kRowHeight - renderer.text.getLineHeight(font)) / 2, folderLabel.c_str(), true,
                         EpdFontFamily::REGULAR);
    if (slot > 0) renderer.line.render(0, folderY - 1, width, folderY - 1, true, LineRender::Style::Dotted);
  }
  const int backgroundY = contentTop + kRowHeight * LibraryWidget::kVisibleFolderCount;
  const int labelY = backgroundY + kRowHeight;
  renderer.rectangle.fill(0, backgroundY, width, kRowHeight, static_cast<int>(GfxRenderer::FillTone::Paper));
  renderer.rectangle.fill(0, labelY, width, kRowHeight, static_cast<int>(GfxRenderer::FillTone::Paper));
  renderer.text.render(font, 20, backgroundY + (kRowHeight - renderer.text.getLineHeight(font)) / 2, "Background",
                       true, EpdFontFamily::REGULAR);
  Toggle::render(renderer, width - 20, backgroundY, kRowHeight, background_);
  renderer.text.render(font, 20, labelY + (kRowHeight - renderer.text.getLineHeight(font)) / 2, "Label", true,
                       EpdFontFamily::REGULAR);
  Toggle::render(renderer, width - 20, labelY, kRowHeight, showLabel_);
  renderer.line.render(0, backgroundY - 1, width, backgroundY - 1, true, LineRender::Style::Dotted);
  renderer.line.render(0, labelY - 1, width, labelY - 1, true, LineRender::Style::Dotted);
  if (folderPopup_) renderFolderPopup();
  renderer.displayBuffer();
}

void BaseLibraryActivity::renderFolderPopup() {
  std::vector<std::string> values;
  values.reserve(availableFolders_.size());
  for (const LibraryWidget::Folder& folder : availableFolders_) values.push_back(folder.name);
  const PopUpBounds box = PopUp::bounds(renderer, static_cast<int>(values.size()));
  PopUp::background(renderer, box);
  const std::string title = "Folder " + std::to_string(folderPopupIndex_ + 1);
  PopUp::title(renderer, box, title.c_str());
  PopUp::list(renderer, box, values, folderSelected_[static_cast<size_t>(folderPopupIndex_)], folderPopupScroll_);
  PopUp::border(renderer, box);
}

void BaseLibraryActivity::close() {
  if (onApply_ && !availableFolders_.empty()) {
    FolderPaths result;
    for (int slot = 0; slot < LibraryWidget::kFolderCount; ++slot) {
      result[static_cast<size_t>(slot)] =
          availableFolders_[static_cast<size_t>(folderSelected_[static_cast<size_t>(slot)])].path;
    }
    onApply_(result, background_, showLabel_);
  }
  if (onBack_) onBack_();
}

void BaseLibraryActivity::handleTouch(const int x, const int y) {
  if (folderPopup_) {
    const PopUpBounds box = PopUp::bounds(renderer, static_cast<int>(availableFolders_.size()));
    if (!inside(x, y, box.x, box.y, box.width, box.height)) {
      folderPopup_ = false;
      render();
      return;
    }
    const int optionY = y - box.y - box.header;
    if (optionY < 0 || optionY >= box.rows * box.row) return;
    const int selected = folderPopupScroll_ + optionY / box.row;
    if (selected < static_cast<int>(availableFolders_.size())) {
      folderSelected_[static_cast<size_t>(folderPopupIndex_)] = selected;
      folderPopup_ = false;
      render();
    }
    return;
  }
  const int contentTop = FREEINK_DEVICE_X4PRO ? 80 : 70;
  if (y >= contentTop && y < contentTop + kRowHeight * LibraryWidget::kVisibleFolderCount) {
    folderPopupIndex_ = std::min(LibraryWidget::kVisibleFolderCount - 1, (y - contentTop) / kRowHeight);
    const PopUpBounds box = PopUp::bounds(renderer, static_cast<int>(availableFolders_.size()));
    const int selected = folderSelected_[static_cast<size_t>(folderPopupIndex_)];
    const int maxScroll = std::max(0, static_cast<int>(availableFolders_.size()) - box.rows);
    folderPopupScroll_ = std::max(0, std::min(selected - box.rows + 1, maxScroll));
    folderPopup_ = true;
  } else if (y >= contentTop + kRowHeight * LibraryWidget::kVisibleFolderCount &&
             y < contentTop + kRowHeight * (LibraryWidget::kVisibleFolderCount + 1)) {
    background_ = !background_;
  } else if (y >= contentTop + kRowHeight * (LibraryWidget::kVisibleFolderCount + 1) &&
             y < contentTop + kRowHeight * (LibraryWidget::kVisibleFolderCount + 2)) {
    showLabel_ = !showLabel_;
  } else {
    return;
  }
  render();
}

void BaseLibraryActivity::loop() {
  if (folderPopup_) {
    if (mappedInput.hasTouch()) {
      const bool swipeUp = mappedInput.wasTouchSwipeUp() || mappedInput.wasTouchSwipeUpForRenderer(renderer);
      const bool swipeDown = mappedInput.wasTouchSwipeDown() || mappedInput.wasTouchSwipeDownForRenderer(renderer);
      if (swipeUp || swipeDown) {
        const PopUpBounds box = PopUp::bounds(renderer, static_cast<int>(availableFolders_.size()));
        const int maxScroll = std::max(0, static_cast<int>(availableFolders_.size()) - box.rows);
        if (swipeUp) {
          folderPopupScroll_ = std::min(folderPopupScroll_ + box.rows, maxScroll);
        } else {
          folderPopupScroll_ = std::max(0, folderPopupScroll_ - box.rows);
        }
        render();
        return;
      }
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      folderPopup_ = false;
      render();
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Up) ||
        mappedInput.wasPressed(MappedInputManager::Button::Down)) {
      const int delta = mappedInput.wasPressed(MappedInputManager::Button::Up) ? -1 : 1;
      const int count = static_cast<int>(availableFolders_.size());
      int& selected = folderSelected_[static_cast<size_t>(folderPopupIndex_)];
      selected = (selected + count + delta) % count;
      render();
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      folderPopup_ = false;
      render();
      return;
    }
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    close();
    return;
  }
  if (mappedInput.hasTouch()) {
    float nx = 0.0f;
    float ny = 0.0f;
    if (mappedInput.wasTouchTapInScreen(renderer, nx, ny)) {
      handleTouch(static_cast<int>(nx * renderer.getScreenWidth()),
                  static_cast<int>(ny * renderer.getScreenHeight()));
      return;
    }
  }
}
