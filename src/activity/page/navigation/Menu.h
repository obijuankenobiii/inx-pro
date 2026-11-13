#pragma once

/**
 * @file Menu.h
 * @brief Menu for the redesigned UiPage shell.
 */

class GfxRenderer;
#include "system/MappedInputManager.h"
#include "state/SystemSetting.h"
#if INX_HAS_FRONTLIGHT || FREEINK_DEVICE_X4PRO || FREEINK_DEVICE_MURPHY_M4 || FREEINK_DEVICE_PICCO
#include "LightDrawer.h"
#endif

/** Draws the shared page navigation controls. */
namespace navigation {

class Menu {
 public:
  enum class Action {
    None,
    Opened,
    Closed,
    Refresh,
    Search,
    View,
    Sort,
    Filter,
    Home,
    Library,
    Settings,
    Stats,
    Sync,
    Apps,
  };

  static constexpr int topPadding = (FREEINK_DEVICE_X4PRO || FREEINK_DEVICE_MURPHY_M4 || FREEINK_DEVICE_PICCO) ? 20 : 15;
  static constexpr int iconSize = 40;
  static constexpr int leftMargin = 20;
  static constexpr int bottomPadding = 10;
  static constexpr int bottomSize = 70;
  static constexpr int bottomHeight = bottomSize + bottomPadding;
  static constexpr int height = topPadding + iconSize + bottomPadding;

  /** Bottom space reserved by the tab bar, or zero when the hidden menu replaces it. */
  static int bottomInset() { return SETTINGS.hiddenMenu != 0 ? 0 : bottomHeight; }

  /** Touch target for the top-left hamburger control. Murphy needs a larger target because its digitizer is less forgiving near small controls. */
  static bool hamburgerHit(const int x, const int y) {
#if FREEINK_DEVICE_MURPHY_M4
    constexpr int touchPadding = 28;
#else
    constexpr int touchPadding = 10;
#endif
    return x >= leftMargin - touchPadding && x < leftMargin + iconSize + touchPadding &&
           y >= topPadding - touchPadding && y < topPadding + iconSize + touchPadding;
  }

  explicit Menu(GfxRenderer& renderer);
  virtual ~Menu() = default;

  virtual const char* name() const { return ""; }
  virtual bool showStats() const { return true; }
  virtual bool showSettings() const { return true; }
  virtual bool showRefresh() const { return true; }

  bool isOpen() const { return open; }
  void close() const {
    open = false;
    selectedVisible = false;
  }
#if INX_HAS_FRONTLIGHT || FREEINK_DEVICE_X4PRO || FREEINK_DEVICE_MURPHY_M4 || FREEINK_DEVICE_PICCO
  bool isLightDrawerOpen() const { return globalLightDrawer(menuRenderer).isOpen(); }
  bool isLightDrawerSliderDragging() const { return globalLightDrawer(menuRenderer).isSliderDragging(); }
#endif
  void render() const;
  Action handleInput(MappedInputManager& input) const;
  Action handleTap(int tapX, int tapY) const;

 protected:
  static constexpr int tabCount = 6;
  int tabSelectorIndex = 0;

  MappedInputManager::Button tabPrevButton() const;
  MappedInputManager::Button tabNextButton() const;
  MappedInputManager::Button itemPrevButton() const;
  MappedInputManager::Button itemNextButton() const;
  void drawBattery(const GfxRenderer& renderer) const;
  void drawHeaderStatus(int rightMargin = 20) const;
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
  mutable int selected = 0;
  mutable bool selectedVisible = false;

  void navigation() const;
  void bottom() const;
  void popup() const;
  Action choose() const;
};

}
