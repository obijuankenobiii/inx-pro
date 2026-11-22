#include "ShortcutSettingsActivity.h"

#include <GfxRenderer.h>

#include <algorithm>

#include "activity/page/SubPage.h"
#include "activity/page/components/global/Toggle.h"
#include "activity/page/components/widget/Shortcut.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"

void ShortcutSettingsActivity::onEnter() { render(); }

int ShortcutSettingsActivity::visibleRows() const {
  return std::max(1, (renderer.getScreenHeight() - SubPage::header(renderer, "Shortcuts", "Widget") - kBottomMargin) /
                           kRowHeight);
}

int ShortcutSettingsActivity::maxScroll() const { return std::max(0, kItemCount - visibleRows()); }

bool ShortcutSettingsActivity::isSelected(const int item) const {
  for (const uint8_t selected : selectedItems_) {
    if (selected == item) return true;
  }
  return false;
}

int ShortcutSettingsActivity::selectedCount() const {
  int count = 0;
  for (const uint8_t selected : selectedItems_) {
    if (selected < kItemCount) ++count;
  }
  return count;
}

void ShortcutSettingsActivity::toggle(const int item) {
  for (int slot = 0; slot < kSlotCount; ++slot) {
    if (selectedItems_[slot] == item) {
      for (int next = slot; next + 1 < kSlotCount; ++next) selectedItems_[next] = selectedItems_[next + 1];
      selectedItems_[kSlotCount - 1] = kItemCount;
      render();
      return;
    }
  }
  if (selectedCount() >= kSlotCount) return;
  for (int slot = 0; slot < kSlotCount; ++slot) {
    if (selectedItems_[slot] >= kItemCount) {
      selectedItems_[slot] = static_cast<uint8_t>(item);
      render();
      return;
    }
  }
}

void ShortcutSettingsActivity::close() {
  if (onApply_) onApply_(selectedItems_);
  if (onBack_) onBack_();
}

void ShortcutSettingsActivity::render() {
  renderer.clearScreen();
  const int contentTop = SubPage::header(renderer, "Shortcuts", "Widget");
  const int font = systemFontId();
  const int width = renderer.getScreenWidth();
  const int rows = visibleRows();
  const int lineHeight = renderer.text.getLineHeight(font);
  for (int row = 0; row < rows; ++row) {
    const int item = scrollOffset_ + row;
    if (item >= kItemCount) break;
    const int y = contentTop + row * kRowHeight;
    renderer.rectangle.fill(0, y, width, kRowHeight, static_cast<int>(GfxRenderer::FillTone::Paper));
    renderer.text.render(font, 20, y + (kRowHeight - lineHeight) / 2, Shortcut::itemLabel(item), true,
                         EpdFontFamily::REGULAR);
    Toggle::render(renderer, width - 24, y, kRowHeight, isSelected(item), false);
    if (row + 1 < rows && item + 1 < kItemCount) {
      renderer.line.render(20, y + kRowHeight - 1, width - 20, y + kRowHeight - 1, true,
                           LineRender::Style::Dotted);
    }
  }
  mappedInput.mapLabels("\xC2\xAB Back", "", "Up", "Down");
  renderer.displayBuffer();
}

void ShortcutSettingsActivity::loop() {
  if (SubPage::closeInput(renderer, mappedInput, [this] { close(); })) return;
  if (mappedInput.hasTouch()) {
    if (mappedInput.wasTouchSwipeUpForRenderer(renderer)) {
      scrollOffset_ = std::min(maxScroll(), scrollOffset_ + visibleRows());
      render();
      return;
    }
    if (mappedInput.wasTouchSwipeDownForRenderer(renderer)) {
      scrollOffset_ = std::max(0, scrollOffset_ - visibleRows());
      render();
      return;
    }
    float nx = 0.0f;
    float ny = 0.0f;
    if (mappedInput.wasTouchTapInScreen(renderer, nx, ny)) {
      const int y = static_cast<int>(ny * renderer.getScreenHeight());
      const int contentTop = SubPage::header(renderer, "Shortcuts", "Widget");
      if (y >= contentTop && y < contentTop + visibleRows() * kRowHeight) {
        const int item = scrollOffset_ + (y - contentTop) / kRowHeight;
        if (item >= 0 && item < kItemCount) toggle(item);
      }
      return;
    }
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    selectedIndex_ = std::max(0, selectedIndex_ - 1);
    if (selectedIndex_ < scrollOffset_) scrollOffset_ = selectedIndex_;
    render();
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    selectedIndex_ = std::min(kItemCount - 1, selectedIndex_ + 1);
    if (selectedIndex_ >= scrollOffset_ + visibleRows()) scrollOffset_ = selectedIndex_ - visibleRows() + 1;
    render();
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    toggle(selectedIndex_);
  }
}
