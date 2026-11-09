#pragma once

/**
 * @file SubPage.h
 * @brief Shared shell for pages opened from another page.
 */

#include <functional>

#include "Page.h"

class SubPage : public UiPage {
 public:
  SubPage(const char* name, GfxRenderer& renderer, MappedInputManager& mappedInput,
          std::function<void()> close);
  ~SubPage() override = default;

  void loop() override;

  /** Draws the shared sub-page title and top-right close button. */
  static int header(const GfxRenderer& renderer, const char* name);
  /** Draws the shared title, an adjacent gray mode value, and close button. */
  static int header(const GfxRenderer& renderer, const char* name, const char* mode);
  /** Draws the shared close X from its native 40x40 bitmap at the requested size. */
  static void closeIcon(const GfxRenderer& renderer, int x, int y, int size);
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
