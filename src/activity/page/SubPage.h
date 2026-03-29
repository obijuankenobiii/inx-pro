#pragma once

/**
 * @file SubPage.h
 * @brief Shared shell for pages opened from another page.
 */

#include <functional>

#include "Page.h"

class SubPage : public Page {
 public:
  SubPage(const char* name, GfxRenderer& renderer, MappedInputManager& mappedInput,
          std::function<void()> close);
  ~SubPage() override = default;

  void loop() override;

  /** Draws the shared sub-page title and top-right close button. */
  static int header(const GfxRenderer& renderer, const char* name);
  /** Handles the shared close button, Back button, and swipe-up gesture. */
  static bool closeInput(GfxRenderer& renderer, MappedInputManager& mappedInput,
                         const std::function<void()>& close, bool closeOnSwipeUp = true);

 protected:
  void menu() override;
  bool closeInput(bool closeOnSwipeUp = true);
  void dismiss();

 private:
  std::function<void()> close;
};
