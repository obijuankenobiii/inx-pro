#pragma once

/**
 * @file SettingsDrawer.h
 * @brief Public interface and types for SettingsDrawer.
 */

#include <GfxRenderer.h>

#include <array>
#include <cstddef>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "state/BookSetting.h"
#include "system/MappedInputManager.h"

/**
 * @class SettingsDrawer
 * @brief A drawer UI component for modifying book reading settings
 *
 * Provides a settings menu for adjusting font, layout, system, and status
 * bar settings while reading. Both the preset editor and in-book drawer
 * present the controls as top tabs.
 */
class SettingsDrawer {
 public:
  /**
   * @brief Constructs a new SettingsDrawer
   * @param renderer Reference to the graphics renderer
   * @param settings Reference to book settings to modify
   * @param onSettingsChanged Callback triggered when settings are changed
   */
  SettingsDrawer(GfxRenderer& renderer, BookSettings& settings, std::function<void()> onSettingsChanged);

  /**
   * @brief Destructor
   */
  ~SettingsDrawer();

  /**
   * @brief Shows the settings drawer
   */
  void show();

  /**
   * @brief Hides the settings drawer
   */
  void hide();

  /** Restore the reader frame captured when the standalone drawer was opened. */
  bool restoreStoredFrame();

  /** Discard the captured reader frame before a full page rebuild. */
  void discardStoredFrame();

  /**
   * @brief Renders the settings drawer
   */
  void render();

  /**
   * @brief Handles input for the settings drawer
   * @param input Reference to the input manager
   */
  void handleInput(MappedInputManager& input);

  /**
   * @brief Checks if the drawer has been dismissed
   * @return true if dismissed, false otherwise
   */
  bool isDismissed() const { return dismissed; }

  /**
   * @brief Checks if settings have been updated
   * @return true if settings were changed, false otherwise
   */
  bool shouldUpdate() const { return settingsUpdated; }

  /**
   * @brief Clears the settings updated flag
   */
  void clearUpdateFlag() { settingsUpdated = false; }

  /** Recompute drawer geometry and menu rows after renderer orientation or screen size changes. */
  void relayoutForRendererChange();


  /**
   * @brief Renders the drawer inside a fixed sub-region instead of the default full-screen overlay.
   *
   * Used by the preset editor to place the settings list in the bottom half of the screen. While an
   * embedded region is set, the host callback composites the rest of the screen and the
   * drawer commits the combined framebuffer once after that callback returns.
   */
  void setEmbeddedRegion(int x, int y, int w, int h);

  /** Largest height <= maxHeight that fits a whole number of menu rows (no dead space below the list). */
  int snapEmbeddedHeight(int maxHeight) const;

  /** Host callback invoked whenever the embedded drawer needs the rest of the screen redrawn. */
  void setEmbeddedInvalidate(std::function<void()> cb) { onEmbeddedInvalidate_ = std::move(cb); }

  /** True while an embedded region is active. */
  bool isEmbedded() const { return embedded_; }

  /** True while the independent margin editor owns drawer input. */
  bool isMarginEditorOpen() const { return marginEditorOpen_; }

  /** True when reader touch actions are enabled. */
  bool isTouchEnabled() const { return touchEnabled_; }

  /** Sets the persisted reader touch state when the drawer is opened. */
  void setTouchEnabled(bool enabled) { touchEnabled_ = enabled; }

  /** Called immediately when the in-book touch setting changes. */
  void setTouchEnabledChanged(std::function<void(bool)> callback) {
    onTouchEnabledChanged_ = std::move(callback);
  }

 private:
  /**
   * @enum GroupType
   * @brief Types of setting groups for organization
   */
  enum class GroupType {
    FONT,
    LAYOUT,
    IMAGE,
    CONTROLS,
    STATUS_BAR,
    STATUS_BAR_FULL
  };
  static constexpr size_t kGroupCount = 6;
  static constexpr size_t groupIndex(const GroupType group) { return static_cast<size_t>(group); }
  bool isGroupExpanded(GroupType group) const { return groupExpanded_[groupIndex(group)]; }

  /**
   * @enum MenuItem
   * @brief All available menu items in the settings drawer
   */
  enum class MenuItem {

    Separator,
    StatusBarSeparator,
    StatusBarFullSeparator,
    PresetPicker,

    FontFamily,
    FontSize,

    LineHeight,
    TextSpace,
    Alignment,
    ExtraParagraphSpacing,
    ParagraphCssIndent,
    Margins,
    ReadingOrientation,

    Hyphenation,
    BionicReading,
    DarkMode,
    ReadingGuideLines,
    AntiAliasing,
    RefreshRate,
    ReaderPowerButton,
    ChapterSkip,
    NavigationLock,

    StatusBarLeft,
    StatusBarMiddle,
    StatusBarRight,

    /** The Full bar is a single full-width loading/progress bar (see StatusBar::kFullBarStyles),
     *  not 3 independent text sections like the main bar - one row, not three. */
    StatusBarFullStyle,

    ReaderImageGrayscale,
    PageAutoTurn
  };

  /**
   * @struct MenuEntry
   * @brief Represents a single menu item in the settings drawer
   */
  struct MenuEntry {
    MenuItem item;
    GroupType group;
    const char* name;
    std::function<const char*(const BookSettings&)> getValueText;
    std::function<void(BookSettings&, int)> change;
  };

  GfxRenderer& renderer;
  BookSettings& settings;
  std::function<void()> onSettingsChanged;
  std::function<void(bool)> onTouchEnabledChanged_;

  bool embedded_ = false;
  std::function<void()> onEmbeddedInvalidate_;
  bool visible = false;
  bool dismissed = false;
  bool storedFrame_ = false;
  int selectedIndex = 0;
  int drawerHeight;
  int drawerY;
  int drawerX = 0;
  int drawerWidth = 0;
  int itemHeight = 60;
  int itemsPerPage;
  int scrollOffset = 0;
  uint32_t lastInputTime;

  bool settingsUpdated = false;
  bool touchEnabled_ = true;

  bool selectorOpen_ = false;
  int selectorMenuIndex_ = -1;
  int selectorSelected_ = 0;
  int selectorScroll_ = 0;
  std::vector<std::string> selectorOptions_;
  uint8_t* selectorFrame_ = nullptr;
  size_t selectorFrameSize_ = 0;
  bool selectorFrameRestored_ = false;
  GroupType selectorGroup_ = GroupType::FONT;

  bool marginEditorOpen_ = false;
  int marginEditorSelected_ = 0;
  uint32_t suppressBackReleaseUntil_ = 0;

  std::array<bool, kGroupCount> groupExpanded_{};
  GroupType selectedGroup_ = GroupType::FONT;
  std::vector<MenuEntry> menuItems;
  bool frontlightDragging_ = false;
  bool frontlightDragChanged_ = false;
  bool frontlightCaretPressed_ = false;
  uint8_t frontlightDragControl_ = 0;

  /**
   * @brief Renders the drawer with specified refresh mode
   * @param mode Display refresh mode to use
   */
  void renderWithRefresh(HalDisplay::RefreshMode mode);

  /**
   * @brief Sets up the menu structure based on expansion states
   */
  void setupMenu();

  /**
   * @brief Draws the background panel
   */
  void drawBackground();

  /**
   * @brief Draws all menu items
   */
  void drawMenuItems();

  /** Draw the preset editor's Font, Layout, Bar, and Settings tabs. */
  void drawTabs();

  void drawMenuItemRow(int visibleRow, int menuIndex);
  void drawMarginPreview(int x, int y, int width, int height);
  void drawMarginEditor();
  bool handleMarginEditorInput(MappedInputManager& input);
  void openMarginEditor();
  void closeMarginEditor();
  void applyMarginChange(int delta);

  bool isDropdownItem(MenuItem item) const;
  bool selectorOpensUpward() const;
  void openSelector(int menuIndex);
  void closeSelector();
  void captureSelectorFrame();
  void restoreSelectorFrame();
  void clearSelectorFrame();
  void commitSelectorSelection();
  void drawSelectorPopup();
  bool handleSelectorInput(MappedInputManager& input);

  /**
   * @brief Draws the scroll indicator
   */
  void drawScrollIndicator();

  void drawFrontlightBar();
  bool handleFrontlightInput(MappedInputManager& input, uint32_t currentTime);

  void clearScrollIndicatorArea();

  void syncLayoutFromRenderer();

  void refreshSelectionRows(int previousIndex, bool redrawScrollIndicator);

  /**
   * @brief Applies a delta change to the selected menu item
   * @param delta Change amount (-1 or 1)
   */
  void applyChange(int delta);

  /**
   * @brief Toggles expansion state of a group
   * @param group Group to toggle
   */
  void toggleGroup(GroupType group);

  /** Replace the preset editor's visible list with the selected tab's settings. */
  void selectGroup(GroupType group);
};
