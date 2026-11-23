#pragma once

#include <cstdint>
#include <functional>

#include "activity/Activity.h"

class ShortcutSettingsActivity final : public Activity {
 public:
  using ApplyCallback = std::function<void(const uint8_t*)>;

  ShortcutSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const uint8_t* selectedItems,
                           ApplyCallback onApply, std::function<void()> onBack)
      : Activity("ShortcutSettings", renderer, mappedInput), onApply_(std::move(onApply)), onBack_(std::move(onBack)) {
    for (int index = 0; index < 4; ++index) selectedItems_[index] = selectedItems ? selectedItems[index] : index;
  }

  void onEnter() override;
  void loop() override;

 private:
  static constexpr int kItemCount = 10;
  static constexpr int kSlotCount = 4;
  static constexpr int kRowHeight = 70;
  static constexpr int kBottomMargin = 20;

  uint8_t selectedItems_[kSlotCount] = {};
  int scrollOffset_ = 0;
  int selectedIndex_ = 0;
  ApplyCallback onApply_;
  std::function<void()> onBack_;

  int visibleRows() const;
  int maxScroll() const;
  bool isSelected(int item) const;
  int selectedCount() const;
  void toggle(int item);
  void close();
  void render();
};
