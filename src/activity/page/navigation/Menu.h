#pragma once

/**
 * @file Menu.h
 * @brief Menu for the redesigned Page shell.
 */

class GfxRenderer;
#include "system/MappedInputManager.h"
#if FREEINK_DEVICE_X4PRO
#include "LightDrawer.h"
#endif

/** Draws the shared page navigation controls. */
namespace navigation {

class Menu {
 public:
  enum class Action { None, Opened, Closed, Refresh, Search, View, Sort, Filter, Home, Library, Settings, Stats, Sync };

  static constexpr int topPadding = FREEINK_DEVICE_X4PRO ? 25 : 15;
  static constexpr int iconSize = 40;
  static constexpr int leftMargin = 20;
  static constexpr int bottomPadding = 10;
  static constexpr int bottomSize = 70;
  static constexpr int bottomHeight = bottomSize + bottomPadding;
  static constexpr int height = topPadding + iconSize + bottomPadding;

  explicit Menu(GfxRenderer& renderer);
  virtual ~Menu() = default;

  virtual const char* name() const { return ""; }
  virtual bool showStats() const { return true; }
  virtual bool showSettings() const { return true; }
  virtual bool showRefresh() const { return true; }

  bool isOpen() const { return open; }
#if FREEINK_DEVICE_X4PRO
  bool isLightDrawerOpen() const { return lightDrawer.isOpen(); }
  bool isLightDrawerSliderDragging() const { return lightDrawer.isSliderDragging(); }
#endif
  void render() const;
  Action handleInput(MappedInputManager& input) const;
  Action handleTap(int tapX, int tapY) const;

 protected:
  static constexpr int tabCount = 5;
  int tabSelectorIndex = 0;

  MappedInputManager::Button tabPrevButton() const;
  MappedInputManager::Button tabNextButton() const;
  MappedInputManager::Button itemPrevButton() const;
  MappedInputManager::Button itemNextButton() const;
  void drawBattery(const GfxRenderer& renderer) const;
  virtual void navigateToSelectedMenu() {}
  void handleTabNavigation(bool leftPressed, bool rightPressed);
  bool handleTabBarTouch(MappedInputManager& mappedInput, const GfxRenderer& renderer);

  virtual void title() const;
  virtual void center() const {}
  virtual Action centerTap(int tapX, int tapY) const {
    (void)tapX;
    (void)tapY;
    return Action::None;
  }

 private:
  GfxRenderer& menuRenderer;
  mutable bool open = false;
  mutable bool ignoreOpeningTap = false;
  mutable int openingX = -1;
  mutable int openingY = -1;
  mutable int selected = 0;
  mutable bool selectedVisible = false;
#if FREEINK_DEVICE_X4PRO
  mutable LightDrawer lightDrawer;
#endif

  void navigation() const;
  void bottom() const;
  void popup() const;
  Action choose() const;
};

}
