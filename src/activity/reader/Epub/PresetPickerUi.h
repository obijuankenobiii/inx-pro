#pragma once

/**
 * @file PresetPickerUi.h
 * @brief Popup for quickly applying a saved reader preset from the EPUB reader.
 */

class EpubActivity;

class PresetPickerUi {
 public:
  bool isActive() const { return mode_; }

  void enter(EpubActivity& act);
  void handleInput(EpubActivity& act);

 private:
  void clampScroll();
  void render(EpubActivity& act);

  bool mode_ = false;
  int selected_ = 0;
  int scroll_ = 0;
};
