#pragma once

/**
 * @file Page.h
 * @brief Minimal page foundation for the new page redesign.
 */

#include "../Activity.h"
#include "system/UiLayout.h"
#include "navigation/Menu.h"

/**
 * Shared shell for the redesigned pages.
 *
 * The shell deliberately knows nothing about the existing Recent/Library
 * activities. It only paints the new top menu and delegates page content to
 * the derived page.
 */
class Page : public Activity, public navigation::Menu {
 public:
  // --- Shared page layout ---------------------------------------------------------------
  // Aliases for UiLayout so page code can write Page::LIST_ITEM_HEIGHT. The values live in
  // system/UiLayout.h — a dependency-free header the reader can also include, which Page.h
  // cannot be because lib/Epub/Epub/Page.h declares a colliding `class Page`.
  static constexpr int LIST_ITEM_HEIGHT = UiLayout::LIST_ITEM_HEIGHT;      ///< one row in any settings/menu list
  static constexpr int HEADER_HEIGHT = UiLayout::HEADER_HEIGHT;         ///< in-page header band
  static constexpr int PAGE_HEADER_HEIGHT = UiLayout::PAGE_HEADER_HEIGHT;    ///< taller header used by drawer pages
  static constexpr int TAB_BAR_HEIGHT = UiLayout::TAB_BAR_HEIGHT;        ///< bottom navigation strip
  static constexpr int CONTENT_TOP = UiLayout::CONTENT_TOP;           ///< first usable y below the status row
  static constexpr int CONTENT_BOTTOM_PADDING = UiLayout::CONTENT_BOTTOM_PADDING; ///< gap between content and the tab bar
  static constexpr int LIST_BOTTOM_PADDING = UiLayout::LIST_BOTTOM_PADDING;

  /** Last usable y for page content, i.e. just above the bottom tab bar. */
  static int contentBottomY(const GfxRenderer& renderer);
  /** Top edge of the bottom tab bar. */
  static int tabBarTopY(const GfxRenderer& renderer);

  Page(const char* name, GfxRenderer& renderer, MappedInputManager& mappedInput);
  ~Page() override = default;

  bool allowGlobalPowerRefresh() override { return true; }
  void onEnter() override;
  virtual void loop() override;

 protected:
  virtual void menu();
  virtual void refresh();
  virtual void search();
  virtual bool back();
 virtual bool menuAction(navigation::Menu::Action action);
  virtual void content() = 0;
  /** Handles only shared navigation taps and leaves ordinary page taps buffered for the child view. */
  bool menuInput();
  /** Renders the page when its content or menu state changed. */
  void renderPage();
  bool updateRequired = false;
  bool routeMenuAction(navigation::Menu::Action action);

 private:
  void render();
};
