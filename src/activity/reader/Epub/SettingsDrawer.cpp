/**
 * @file SettingsDrawer.cpp
 * @brief Definitions for SettingsDrawer.
 */

#include "SettingsDrawer.h"
#include "system/UiLayout.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#if defined(ARDUINO_ARCH_ESP32)
#include <esp_heap_caps.h>
#endif

#include "../../settings/ReaderFontSettingsDraw.h"
#include "StatusBar.h"
#include "images/AlignCenter.h"
#include "images/AlignCss.h"
#include "images/AlignJustify.h"
#include "images/AlignLeft.h"
#include "images/AlignRight.h"
#if FREEINK_DEVICE_X4PRO
#include "images/Temp.h"
#include "images/LightOff.h"
#include "images/LightOn.h"
#include "system/Frontlight.h"
#endif
#include "images/LibraryFilterLeft.h"
#include "images/LibraryFilterRight.h"
#include "images/PresetBars.h"
#include "images/PresetFont.h"
#include "images/PresetLayout.h"
#include "images/PresetSettings.h"
#include "images/Rotate.h"
#include "images/Touch.h"
#include "state/ReaderPreset.h"
#include "state/SystemSetting.h"
#include "system/FontManager.h"
#include "system/Fonts.h"

// Same value as Page::LIST_ITEM_HEIGHT. Not taken from that header: the reader pulls in
// lib/Epub/Epub/Page.h via Section.h, and the two `Page` classes collide.
constexpr int LIST_ITEM_HEIGHT = UiLayout::LIST_ITEM_HEIGHT;

namespace {
const char* statusBarItemName(const StatusBarItem item) {
  static const char* names[] = {"None",       "Page Numbers",   "Percentage",   "Chapter Title",      "Battery Icon",
                                "Battery %",  "Battery Icon+%", "Progress Bar", "Progress Bar+%",     "Page Bars",
                                "Book Title", "Author Name",    "Page Num+%",   "Time Left (Chapter)", "Time Left (Book)",
                                "Clock"};
  int index = static_cast<int>(item);
  if (index < 0 || index >= static_cast<int>(StatusBarItem::STATUS_BAR_ITEM_COUNT)) {
    index = 0;
  }
  return names[index];
}

constexpr int tabSize = 40;
constexpr int tabVerticalPadding = FREEINK_DEVICE_X4PRO ? 20 : 14;
constexpr int tabPadding = tabVerticalPadding;
constexpr int tabHeight = tabSize + tabPadding * 2;
constexpr int selectorRows = 5;
constexpr int selectorRowHeight = LIST_ITEM_HEIGHT;
constexpr int standaloneRows = 5;
constexpr int kBottomControlHeight = 140;
#if FREEINK_DEVICE_X4PRO
constexpr int kFrontlightRowHeight = kBottomControlHeight / 2;
constexpr int kFrontlightCaretSize = 30;
constexpr int kFrontlightCaretGap = 8;
constexpr int kFrontlightCaretTouchPadding = 10;
constexpr int kFrontlightIconSize = 40;
constexpr int kFrontlightTrackHeight = 4;
constexpr int kFrontlightKnobRadius = 12;
constexpr int kFrontlightKnobBorder = 2;
constexpr int kFrontlightControlPadding = 20;
#endif

uint8_t* allocateSelectorFrame(const size_t size) {
#if defined(ARDUINO_ARCH_ESP32)
  if (void* memory = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)) {
    return static_cast<uint8_t*>(memory);
  }
  return static_cast<uint8_t*>(heap_caps_malloc(size, MALLOC_CAP_8BIT));
#else
  return static_cast<uint8_t*>(std::malloc(size));
#endif
}

void freeSelectorFrame(uint8_t* frame) {
  if (!frame) return;
#if defined(ARDUINO_ARCH_ESP32)
  heap_caps_free(frame);
#else
  std::free(frame);
#endif
}

int drawerListTop() { return tabHeight + (FREEINK_DEVICE_X4PRO ? 5 : 1); }
constexpr int kDrawerListBottomPadding = 12;

struct SelectorBounds {
  int x;
  int y;
  int width;
  int height;
  int rows;
};

SelectorBounds selectorBounds(const int drawerX, const int drawerWidth, const int fieldY, const int fieldHeight,
                              const int screenHeight, const int rows, const bool openUpward) {
  const int left = drawerX + drawerWidth * 40 / 100;
  const int right = drawerX + drawerWidth - 24;
  const int width = std::max(80, right - left);
  const int available = openUpward ? std::max(1, fieldY - 1)
                                   : std::max(1, screenHeight - std::max(0, std::min(fieldY + fieldHeight, screenHeight - 1)) - 1);
  const int visibleRows = std::max(1, std::min(rows, available / selectorRowHeight));
  const int height = visibleRows * selectorRowHeight + 1;
  const int y = openUpward ? std::max(0, fieldY - height)
                           : std::max(0, std::min(fieldY + fieldHeight, screenHeight - 1));
  return {left, y, width, height, visibleRows};
}

bool isLandscapeReader(const GfxRenderer& gfx) {
  const auto o = gfx.getOrientation();
  return o == GfxRenderer::LandscapeClockwise || o == GfxRenderer::LandscapeCounterClockwise;
}

void drawSettingsDropdown(const GfxRenderer& renderer, int left, int right, int itemY, int itemHeight,
                          const char* value) {
  constexpr int kPadX = 10;
  const int boxY = itemY + 8;
  const int boxH = itemHeight - 16;
  renderer.rectangle.render(left, boxY, right - left, boxH, true, false);

  const int textMaxW = std::max(1, right - left - 32);
  const std::string shown = renderer.text.truncate(MONTSERRAT_8_FONT_ID, value ? value : "", textMaxW,
                                                    EpdFontFamily::REGULAR);
  const int textY = boxY + (boxH - renderer.text.getLineHeight(MONTSERRAT_8_FONT_ID)) / 2;
  renderer.text.render(MONTSERRAT_8_FONT_ID, left + kPadX, textY, shown.c_str(), true,
                       EpdFontFamily::REGULAR);

  const int chevronX = right - 14;
  const int chevronY = boxY + boxH / 2 - 2;
  renderer.line.render(chevronX - 3, chevronY, chevronX, chevronY + 3, true);
  renderer.line.render(chevronX, chevronY + 3, chevronX + 3, chevronY, true);
}

void drawValueStepper(GfxRenderer& renderer, const char* value, int left, int right, int itemY, int itemHeight) {
  constexpr int iconSize = 30;
  constexpr int gap = 8;
  const int valueW = renderer.text.getWidth(MONTSERRAT_10_FONT_ID, value, EpdFontFamily::REGULAR);
  const int width = iconSize + gap + valueW + gap + iconSize;
  const int x = std::max(left, right - width);
  const int iconY = itemY + (itemHeight - iconSize) / 2 + 5;
  const int textY = itemY + (itemHeight - renderer.text.getLineHeight(MONTSERRAT_10_FONT_ID)) / 2;

  renderer.bitmap.icon(LibraryFilterLeft, x, iconY, iconSize, iconSize);
  renderer.text.render(MONTSERRAT_10_FONT_ID, x + iconSize + gap, textY, value, true, EpdFontFamily::REGULAR);
  renderer.bitmap.icon(LibraryFilterRight, x + iconSize + gap + valueW + gap, iconY, iconSize, iconSize);
}

void drawJustificationSegments(const GfxRenderer& renderer, int left, int right, int itemY, int itemHeight,
                               int selectedIndex) {
  // The center/right bitmap assets are named in the opposite order; keep the setting values unchanged and
  // place the correct glyph over each alignment value.
  static constexpr const uint8_t* kIcons[] = {AlignJustify, AlignLeft, AlignRight, AlignCenter, AlignCss};
  constexpr int kCount = sizeof(kIcons) / sizeof(kIcons[0]);
  const int boxY = itemY + 7;
  const int boxH = itemHeight - 14;
  const int segmentW = std::max(1, (right - left) / kCount);

  for (int i = 0; i < kCount; ++i) {
    const int segmentX = left + i * segmentW;
    const int width = (i == kCount - 1) ? right - segmentX : segmentW;
    const bool selected = i == selectedIndex;
    renderer.rectangle.fill(segmentX, boxY, width, boxH, selected, false);
    renderer.rectangle.render(segmentX, boxY, width, boxH, true, false);
    constexpr int kIconSize = 30;
    const int iconX = segmentX + std::max(0, (width - kIconSize) / 2);
    const int iconY = boxY + std::max(0, (boxH - kIconSize) / 2);
    renderer.bitmap.icon(kIcons[i], iconX, iconY, kIconSize, kIconSize, BitmapRender::Orientation::None, selected);
  }
}

/** List selection: portrait uses Up/Down only so Left/Right stay for value edits (matches pre-drawer UX). */
bool readSettingsListPrev(const MappedInputManager& in, const GfxRenderer& r) {
  if (isLandscapeReader(r)) {
    return in.wasPressed(MappedInputManager::Button::Right);
  }
  return in.wasPressed(MappedInputManager::Button::Up);
}

bool readSettingsListNext(const MappedInputManager& in, const GfxRenderer& r) {
  if (isLandscapeReader(r)) {
    return in.wasPressed(MappedInputManager::Button::Left);
  }
  return in.wasPressed(MappedInputManager::Button::Down);
}

/** Portrait: Left/Right adjust values. Landscape: Down/Up (swap with list so value edits match device). */
bool readValueDecrease(const MappedInputManager& in, const GfxRenderer& r) {
  if (isLandscapeReader(r)) {
    return in.wasPressed(MappedInputManager::Button::Down);
  }
  return in.wasPressed(MappedInputManager::Button::Left);
}

bool readValueIncrease(const MappedInputManager& in, const GfxRenderer& r) {
  if (isLandscapeReader(r)) {
    return in.wasPressed(MappedInputManager::Button::Up);
  }
  return in.wasPressed(MappedInputManager::Button::Right);
}

}  // namespace

/**
 * @brief Constructs a new SettingsDrawer
 * @param renderer Reference to the graphics renderer
 * @param settings Reference to book settings to modify
 * @param onSettingsChanged Callback triggered when settings are changed
 */
SettingsDrawer::SettingsDrawer(GfxRenderer& renderer, BookSettings& settings, std::function<void()> onSettingsChanged)
    : renderer(renderer),
      settings(settings),
      onSettingsChanged(onSettingsChanged),
      lastInputTime(0),
      settingsUpdated(false) {
  itemHeight = LIST_ITEM_HEIGHT;
  syncLayoutFromRenderer();

  selectedIndex = 0;
  scrollOffset = 0;
  visible = false;
  dismissed = false;

  groupExpanded_.fill(false);

  setupMenu();
}

/**
 * @brief Destructor
 */
SettingsDrawer::~SettingsDrawer() { clearSelectorFrame(); }

void SettingsDrawer::setEmbeddedRegion(int x, int y, int w, int h) {
  embedded_ = true;
  drawerX = x;
  drawerY = y;
  drawerWidth = w;
  drawerHeight = h;
  itemsPerPage = std::max(1, (drawerHeight - drawerListTop() - kDrawerListBottomPadding) / itemHeight);
  // The drawer is constructed before the preset editor supplies its embedded region. Rebuild here
  // so it starts on the Font tab instead of the standalone in-book list.
  setupMenu();
}

int SettingsDrawer::snapEmbeddedHeight(int maxHeight) const {
  // Largest height <= maxHeight that fits a whole number of rows under the header, so the embedded
  // drawer has no dead space below the last row.
  const int usable = maxHeight - drawerListTop() - kDrawerListBottomPadding;
  // This is called before setEmbeddedRegion(), so use the embedded editor's row height explicitly;
  // the standalone in-book panel uses a shorter compact row height.
  const int rows = std::min(5, std::max(1, usable / LIST_ITEM_HEIGHT));
  return drawerListTop() + rows * LIST_ITEM_HEIGHT + kDrawerListBottomPadding;
}

void SettingsDrawer::syncLayoutFromRenderer() {
  itemHeight = LIST_ITEM_HEIGHT;
  if (embedded_) {
    // Keep the host-provided region; just recompute how many rows fit.
    itemsPerPage = std::max(1, (drawerHeight - drawerListTop() - kDrawerListBottomPadding) / itemHeight);
    return;
  }
  const int sw = renderer.getScreenWidth();
  const int sh = renderer.getScreenHeight();
  drawerX = 0;
  drawerWidth = sw;
  drawerY = 0;

  // The in-book drawer is always a five-row white panel. Keeping its geometry
  // fixed prevents old rows from a taller tab remaining visible after a tab
  // with fewer settings is selected.
  const int maxRows = std::max(1, (sh - drawerListTop()) / itemHeight);
  itemsPerPage = std::min(standaloneRows, maxRows);
  drawerHeight = drawerListTop() + itemsPerPage * itemHeight + 1;
}

/**
 * @brief Sets up the menu structure based on current expansion states
 */
void SettingsDrawer::setupMenu() {
  menuItems.clear();

  if (selectedGroup_ == GroupType::FONT) {
    MenuEntry fontFamEntry;
    fontFamEntry.item = MenuItem::FontFamily;
    fontFamEntry.group = GroupType::FONT;
    fontFamEntry.name = "Style";
    fontFamEntry.getValueText = [](const BookSettings& s) -> const char* {
      thread_local std::string tls;
      tls = FontManager::readerFontFamilyLabel(s.fontFamily);
      return tls.c_str();
    };
    fontFamEntry.change = [](BookSettings& s, int delta) {
      const int n = static_cast<int>(FontManager::readerFontFamilyOptionCount());
      if (n <= 0) {
        return;
      }
      int newVal = static_cast<int>(s.fontFamily) + delta;
      if (newVal < 0) {
        newVal = n - 1;
      }
      if (newVal >= n) {
        newVal = 0;
      }
      s.fontFamily = static_cast<uint8_t>(newVal);
      FontManager::clampReaderFontFamilySlot(s.fontFamily);
      s.markCustomSettings();
    };
    menuItems.push_back(fontFamEntry);

    MenuEntry fontEntry;
    fontEntry.item = MenuItem::FontSize;
    fontEntry.group = GroupType::FONT;
    fontEntry.name = "Size";
    fontEntry.getValueText = [](const BookSettings& s) -> const char* {
      static const char* sizes[] = {"Extra Small", "Small", "Medium", "Large", "X Large"};
      int index = s.fontSize;
      if (index > 4) index = 1;
      return sizes[index];
    };
    fontEntry.change = [](BookSettings& s, int delta) {
      int newVal = s.fontSize + delta;
      if (newVal >= 0 && newVal <= 4) {
        s.fontSize = newVal;
        s.markCustomSettings();
      }
    };
    menuItems.push_back(fontEntry);

    MenuEntry alignEntry;
    alignEntry.item = MenuItem::Alignment;
    alignEntry.group = GroupType::FONT;
    alignEntry.name = "Alignment";
    alignEntry.getValueText = [](const BookSettings& s) -> const char* {
      static const char* align[] = {"Justify", "Left", "Center", "Right", "Book's style"};
      int index = s.paragraphAlignment;
      if (index > 4) index = 0;
      return align[index];
    };
    alignEntry.change = [](BookSettings& s, int delta) {
      int newVal = s.paragraphAlignment + delta;
      if (newVal >= 0 && newVal <= 4) {
        s.paragraphAlignment = newVal;
        s.markCustomSettings();
      }
    };
    menuItems.push_back(alignEntry);

    MenuEntry hyphenationEntry;
    hyphenationEntry.item = MenuItem::Hyphenation;
    hyphenationEntry.group = GroupType::FONT;
    hyphenationEntry.name = "Hyphenation";
    hyphenationEntry.getValueText = [](const BookSettings& s) -> const char* {
      return s.hyphenationEnabled ? "On" : "Off";
    };
    hyphenationEntry.change = [](BookSettings& s, int) {
      s.hyphenationEnabled = !s.hyphenationEnabled;
      s.markCustomSettings();
    };
    menuItems.push_back(hyphenationEntry);

    // Presets belong to a book, not a preset being edited. Keep this as the
    // final Font-tab row in the in-book drawer so selecting one copies its
    // entire settings snapshot onto the current book.
    if (!embedded_) {
      MenuEntry presetEntry;
      presetEntry.item = MenuItem::PresetPicker;
      presetEntry.group = GroupType::FONT;
      presetEntry.name = "Preset";
      presetEntry.getValueText = [](const BookSettings& s) -> const char* {
        static thread_local std::string name;
        if (s.readerPresetIndex == BookSettings::kNoReaderPreset) {
          return "Custom";
        }
        name = READER_PRESETS.nameOf(s.readerPresetIndex);
        return name.empty() ? "Custom" : name.c_str();
      };
      presetEntry.change = [](BookSettings& s, const int delta) {
        const int count = READER_PRESETS.count();
        if (count <= 0) return;
        int selected = s.readerPresetIndex == BookSettings::kNoReaderPreset ? 0 : s.readerPresetIndex;
        selected = (selected + delta + count) % count;
        READER_PRESETS.applyToBook(selected, s);
      };
      menuItems.push_back(presetEntry);
    }
  }

  if (selectedGroup_ == GroupType::LAYOUT) {
    MenuEntry lineHeightEntry;
    lineHeightEntry.item = MenuItem::LineHeight;
    lineHeightEntry.group = GroupType::LAYOUT;
    lineHeightEntry.name = "Line height";
    lineHeightEntry.getValueText = [](const BookSettings& s) -> const char* {
      static char buf[8];
      snprintf(buf, sizeof(buf), "%d%%", s.lineHeight);
      return buf;
    };
    lineHeightEntry.change = [](BookSettings& s, int delta) {
      int newVal = static_cast<int>(s.lineHeight) + delta * 5;
      if (newVal < 10) newVal = 10;
      if (newVal > 200) newVal = 200;
      s.lineHeight = static_cast<uint8_t>(newVal);
      s.markCustomSettings();
    };
    menuItems.push_back(lineHeightEntry);

    MenuEntry textSpaceEntry;
    textSpaceEntry.item = MenuItem::TextSpace;
    textSpaceEntry.group = GroupType::LAYOUT;
    textSpaceEntry.name = "Word spacing";
    textSpaceEntry.getValueText = [](const BookSettings& s) -> const char* {
      static char buf[8];
      snprintf(buf, sizeof(buf), "%d%%", s.textSpace);
      return buf;
    };
    textSpaceEntry.change = [](BookSettings& s, int delta) {
      int newVal = static_cast<int>(s.textSpace) + delta * 5;
      if (newVal < 10) newVal = 10;
      if (newVal > 200) newVal = 200;
      s.textSpace = static_cast<uint8_t>(newVal);
      s.markCustomSettings();
    };
    menuItems.push_back(textSpaceEntry);

    MenuEntry extraParaEntry;
    extraParaEntry.item = MenuItem::ExtraParagraphSpacing;
    extraParaEntry.group = GroupType::LAYOUT;
    extraParaEntry.name = "Extra Paragraph Spacing";
    extraParaEntry.getValueText = [](const BookSettings& s) -> const char* {
      return s.extraParagraphSpacing ? "On" : "Off";
    };
    extraParaEntry.change = [](BookSettings& s, int) {
      s.extraParagraphSpacing = !s.extraParagraphSpacing;
      s.markCustomSettings();
    };
    menuItems.push_back(extraParaEntry);

    MenuEntry cssIndentEntry;
    cssIndentEntry.item = MenuItem::ParagraphCssIndent;
    cssIndentEntry.group = GroupType::LAYOUT;
    cssIndentEntry.name = "Indent";
    cssIndentEntry.getValueText = [](const BookSettings& s) -> const char* {
      return s.paragraphCssIndentEnabled ? "On" : "Off";
    };
    cssIndentEntry.change = [](BookSettings& s, int) {
      s.paragraphCssIndentEnabled = s.paragraphCssIndentEnabled ? 0 : 1;
      s.markCustomSettings();
    };
    menuItems.push_back(cssIndentEntry);

    MenuEntry marginEntry;
    marginEntry.item = MenuItem::ScreenMargin;
    marginEntry.group = GroupType::LAYOUT;
    marginEntry.name = "Screen Margin";
    marginEntry.getValueText = [](const BookSettings& s) -> const char* {
      static char buf[10];
      snprintf(buf, sizeof(buf), "%d px", s.screenMargin);
      return buf;
    };
    marginEntry.change = [](BookSettings& s, int delta) {
      int newVal = s.screenMargin + (delta * 5);
      if (newVal >= 0 && newVal <= 80) {
        s.screenMargin = newVal;
        s.markCustomSettings();
      }
    };
    menuItems.push_back(marginEntry);

  }

  if (selectedGroup_ == GroupType::CONTROLS) {
    MenuEntry orientationEntry;
    orientationEntry.item = MenuItem::ReadingOrientation;
    orientationEntry.group = GroupType::CONTROLS;
    orientationEntry.name = "Orientation";
    orientationEntry.getValueText = [](const BookSettings& s) -> const char* {
      static const char* orientation[] = {"Portrait", "Landscape CW", "Inverted", "Landscape CCW"};
      int index = s.orientation;
      if (index > 3) index = 0;
      return orientation[index];
    };
    orientationEntry.change = [](BookSettings& s, int delta) {
      int newVal = s.orientation + delta;
      if (newVal >= 0 && newVal <= 3) {
        s.orientation = newVal;
        s.markCustomSettings();
      }
    };
    menuItems.push_back(orientationEntry);

    MenuEntry bionicEntry;
    bionicEntry.item = MenuItem::BionicReading;
    bionicEntry.group = GroupType::CONTROLS;
    bionicEntry.name = "Bionic Reading";
    bionicEntry.getValueText = [](const BookSettings& s) -> const char* {
      return s.bionicReadingEnabled ? "On" : "Off";
    };
    bionicEntry.change = [](BookSettings& s, int) {
      s.bionicReadingEnabled = s.bionicReadingEnabled ? 0 : 1;
      s.markCustomSettings();
    };
    menuItems.push_back(bionicEntry);

    if (!embedded_) {
      MenuEntry darkModeEntry;
      darkModeEntry.item = MenuItem::DarkMode;
      darkModeEntry.group = GroupType::CONTROLS;
      darkModeEntry.name = "Dark mode";
      darkModeEntry.getValueText = [](const BookSettings& s) -> const char* {
        return s.darkMode ? "On" : "Off";
      };
      darkModeEntry.change = [](BookSettings& s, int) {
        s.darkMode = s.darkMode ? 0 : 1;
        s.markCustomSettings();
      };
      menuItems.push_back(darkModeEntry);
    }

    MenuEntry guideLinesEntry;
    guideLinesEntry.item = MenuItem::ReadingGuideLines;
    guideLinesEntry.group = GroupType::CONTROLS;
    guideLinesEntry.name = "Guide Lines";
    guideLinesEntry.getValueText = [](const BookSettings& s) -> const char* {
      static const char* styles[] = {"Off", "Grid", "Notebook"};
      int index = s.readingGuideLinesEnabled;
      if (index > 2) index = 0;
      return styles[index];
    };
    guideLinesEntry.change = [](BookSettings& s, int delta) {
      int newVal = s.readingGuideLinesEnabled + delta;
      if (newVal >= 0 && newVal <= 2) {
        s.readingGuideLinesEnabled = newVal;
        s.markCustomSettings();
      }
    };
    menuItems.push_back(guideLinesEntry);
  }

  if (selectedGroup_ == GroupType::STATUS_BAR) {
    MenuEntry statusLeftEntry;
    statusLeftEntry.item = MenuItem::StatusBarLeft;
    statusLeftEntry.group = GroupType::STATUS_BAR;
    statusLeftEntry.name = "Left Section";
    statusLeftEntry.getValueText = [](const BookSettings& s) -> const char* {
      return statusBarItemName(s.statusBarLeft.item);
    };
    statusLeftEntry.change = [](BookSettings& s, int delta) {
      int newVal = static_cast<int>(s.statusBarLeft.item) + delta;
      if (newVal >= 0 && newVal < static_cast<int>(StatusBarItem::STATUS_BAR_ITEM_COUNT)) {
        s.statusBarLeft.item = static_cast<StatusBarItem>(newVal);
        s.markCustomSettings();
      }
    };
    menuItems.push_back(statusLeftEntry);

    MenuEntry statusMiddleEntry;
    statusMiddleEntry.item = MenuItem::StatusBarMiddle;
    statusMiddleEntry.group = GroupType::STATUS_BAR;
    statusMiddleEntry.name = "Middle Section";
    statusMiddleEntry.getValueText = [](const BookSettings& s) -> const char* {
      return statusBarItemName(s.statusBarMiddle.item);
    };
    statusMiddleEntry.change = [](BookSettings& s, int delta) {
      int newVal = static_cast<int>(s.statusBarMiddle.item) + delta;
      if (newVal >= 0 && newVal < static_cast<int>(StatusBarItem::STATUS_BAR_ITEM_COUNT)) {
        s.statusBarMiddle.item = static_cast<StatusBarItem>(newVal);
        s.markCustomSettings();
      }
    };
    menuItems.push_back(statusMiddleEntry);

    MenuEntry statusRightEntry;
    statusRightEntry.item = MenuItem::StatusBarRight;
    statusRightEntry.group = GroupType::STATUS_BAR;
    statusRightEntry.name = "Right Section";
    statusRightEntry.getValueText = [](const BookSettings& s) -> const char* {
      return statusBarItemName(s.statusBarRight.item);
    };
    statusRightEntry.change = [](BookSettings& s, int delta) {
      int newVal = static_cast<int>(s.statusBarRight.item) + delta;
      if (newVal >= 0 && newVal < static_cast<int>(StatusBarItem::STATUS_BAR_ITEM_COUNT)) {
        s.statusBarRight.item = static_cast<StatusBarItem>(newVal);
        s.markCustomSettings();
      }
    };
    menuItems.push_back(statusRightEntry);
  }

  if (selectedGroup_ == GroupType::STATUS_BAR) {
    // A single style row, not 3 Left/Middle/Right rows - Full is one full-width bar restricted to
    // StatusBar::kFullBarStyles (loading/progress visualizations), so there's nothing to put in
    // separate sections.
    MenuEntry fullStyleEntry;
    fullStyleEntry.item = MenuItem::StatusBarFullStyle;
    fullStyleEntry.group = GroupType::STATUS_BAR_FULL;
    fullStyleEntry.name = "Full Bar";
    fullStyleEntry.getValueText = [](const BookSettings& s) -> const char* {
      return statusBarItemName(static_cast<StatusBarItem>(s.statusBarFullStyle));
    };
    fullStyleEntry.change = [](BookSettings& s, int delta) {
      int idx = 0;
      for (int i = 0; i < StatusBar::kFullBarStyleCount; ++i) {
        if (StatusBar::kFullBarStyles[i] == static_cast<StatusBarItem>(s.statusBarFullStyle)) {
          idx = i;
          break;
        }
      }
      const int newIdx = idx + delta;
      if (newIdx >= 0 && newIdx < StatusBar::kFullBarStyleCount) {
        s.statusBarFullStyle = static_cast<uint8_t>(StatusBar::kFullBarStyles[newIdx]);
        s.markCustomSettings();
      }
    };
    menuItems.push_back(fullStyleEntry);
  }
}

/**
 * @brief Shows the settings drawer
 */
void SettingsDrawer::show() {
  if (visible) return;
  syncLayoutFromRenderer();
  visible = true;
  dismissed = false;
  closeSelector();
  // Touch-first UI: do not present an arbitrary row as selected before the user taps one.
  selectedIndex = -1;
  scrollOffset = 0;
  renderWithRefresh(HalDisplay::FAST_REFRESH);
}

/**
 * @brief Hides the settings drawer
 */
void SettingsDrawer::hide() {
  visible = false;
  dismissed = true;
  closeSelector();
}

void SettingsDrawer::relayoutForRendererChange() {
  syncLayoutFromRenderer();
  setupMenu();
}

/**
 * @brief Renders the settings drawer
 */
void SettingsDrawer::render() {
  if (!visible) return;
  renderWithRefresh(HalDisplay::FAST_REFRESH);
}

/**
 * @brief Renders the settings drawer with specified refresh mode
 * @param mode Display refresh mode to use
 */
void SettingsDrawer::renderWithRefresh(HalDisplay::RefreshMode mode) {
  if (!visible) {
    return;
  }
  // A selector belongs only to the tab that opened it. Never let stale popup
  // state paint over a different tab after a redraw or layout change.
  if (selectorOpen_ && selectorGroup_ != selectedGroup_) {
    closeSelector();
  }
  syncLayoutFromRenderer();
  // The first standalone drawer render follows a page display, which swaps the dual
  // framebuffers. Rebase the writable buffer from the page currently on the panel so
  // the drawer is composited over the book, not the previous activity screen.
  if (!embedded_) {
    renderer.syncWriteBufferFromActive();
  }
  drawBackground();
  drawMenuItems();
  drawScrollIndicator();
  if (selectorOpen_) {
    drawSelectorPopup();
  }
  drawFrontlightBar();
  // Draw the container edge last so the final row divider cannot overwrite it.
  renderer.line.render(drawerX, drawerY + drawerHeight - 1, drawerX + drawerWidth, drawerY + drawerHeight - 1, true);
  if (embedded_) {
    // Host owns the rest of the screen and the display push.
    if (onEmbeddedInvalidate_) onEmbeddedInvalidate_();
    // The host has just displayed the composed preview and drawer. Mirror it
    // into the inactive buffer so a later tab render cannot swap an old
    // selector back onto the screen.
    renderer.syncWriteBufferFromActive();
    return;
  }
  renderer.displayBuffer(mode);
  // displayBuffer swaps Sticky's two framebuffers. Keep the new inactive
  // buffer identical to what was just displayed; the drawer only redraws its
  // panel, while a selector may have painted below that panel.
  renderer.syncWriteBufferFromActive();
}

/**
 * @brief Draws the background panel of the settings drawer
 */
void SettingsDrawer::drawBackground() {
  renderer.rectangle.fill(drawerX, drawerY, drawerWidth, drawerHeight, false);
  renderer.line.render(drawerX, drawerY, drawerX + drawerWidth, drawerY, true);
  drawTabs();
  renderer.line.render(drawerX, drawerY + drawerHeight - 1, drawerX + drawerWidth, drawerY + drawerHeight - 1, true);
}

void SettingsDrawer::drawTabs() {
  struct Tab {
    GroupType group;
    const uint8_t* icon;
    bool touchControl;
    bool rotateControl;
  };
  static constexpr Tab regularTabs[] = {
      {GroupType::FONT, PresetFont, false, false},
      {GroupType::LAYOUT, PresetBars, false, false},
      {GroupType::STATUS_BAR, PresetLayout, false, false},
      {GroupType::CONTROLS, PresetSettings, false, false},
  };
  static constexpr Tab inBookTabs[] = {
      {GroupType::FONT, PresetFont, false, false},
      {GroupType::LAYOUT, PresetBars, false, false},
      {GroupType::STATUS_BAR, PresetLayout, false, false},
      {GroupType::CONTROLS, PresetSettings, false, false},
      {GroupType::FONT, Touch, true, false},
      {GroupType::FONT, Rotate, false, true},
  };

  const Tab* tabs = embedded_ ? regularTabs : inBookTabs;
  const int count = embedded_ ? static_cast<int>(sizeof(regularTabs) / sizeof(regularTabs[0]))
                              : static_cast<int>(sizeof(inBookTabs) / sizeof(inBookTabs[0]));

  const int y = drawerY + 1;
  const int width = std::max(1, drawerWidth / count);
  const int iconOffset = embedded_ ? 0 : 4;
  for (int i = 0; i < count; ++i) {
    const int x = drawerX + i * width;
    const int w = i == count - 1 ? drawerX + drawerWidth - x : width;
    const bool selected = !tabs[i].touchControl && !tabs[i].rotateControl && tabs[i].group == selectedGroup_;
    renderer.rectangle.fill(x, y, w, tabHeight, selected, false);
    renderer.bitmap.icon(tabs[i].icon, x + std::max(0, (w - tabSize) / 2), y + tabPadding + iconOffset, tabSize,
                         tabSize,
                         BitmapRender::Orientation::None, selected);
    if (tabs[i].touchControl && !touchEnabled_) {
      const int iconX = x + std::max(0, (w - tabSize) / 2);
      const int iconY = y + tabPadding + iconOffset;
      // LineRender intentionally supports only horizontal/vertical lines. Draw the strike-through
      // directly so it remains visible across the supplied hand icon.
      for (int offset = 2; offset < tabSize - 2; ++offset) {
        renderer.drawPixel(iconX + offset, iconY + offset, true);
        renderer.drawPixel(iconX + offset + 1, iconY + offset, true);
        renderer.drawPixel(iconX + offset, iconY + offset + 1, true);
      }
    }
  }
  for (int i = 1; i < count; ++i) {
    const int dividerX = drawerX + i * width;
    renderer.line.render(dividerX, y, dividerX, y + tabHeight, true, LineRender::Style::Dotted);
  }
  renderer.line.render(drawerX, y + tabHeight - 1, drawerX + drawerWidth, y + tabHeight - 1, true);
}

/**
 * @brief Draws all menu items in the current scroll view
 */
void SettingsDrawer::drawMenuItems() {
  for (int i = 0; i < itemsPerPage && (i + scrollOffset) < static_cast<int>(menuItems.size()); i++) {
    drawMenuItemRow(i, i + scrollOffset);
  }
}

#if FREEINK_DEVICE_X4PRO
namespace {
int frontlightPercentFromTrack(const int x, const int left, const int width, const bool brightness) {
  const int clamped = std::max(left, std::min(left + width, x));
  const float fraction = static_cast<float>(clamped - left) / static_cast<float>(std::max(1, width));
  if (brightness) return frontlight_ui::percentFromFraction(fraction);
  return static_cast<int>(fraction * 100.0f + 0.5f);
}

bool frontlightPointInCaret(const int x, const int y, const int caretX, const int centerY) {
  return x >= caretX - kFrontlightCaretTouchPadding &&
         x < caretX + kFrontlightCaretSize + kFrontlightCaretTouchPadding &&
         y >= centerY - kFrontlightCaretSize / 2 - kFrontlightCaretTouchPadding &&
         y < centerY + kFrontlightCaretSize / 2 + kFrontlightCaretTouchPadding;
}
}
#endif

void SettingsDrawer::drawFrontlightBar() {
#if FREEINK_DEVICE_X4PRO
  if (embedded_ || !frontlight.present()) return;

  const int barY = renderer.getScreenHeight() - kBottomControlHeight;
  renderer.rectangle.fill(drawerX, barY, drawerWidth, kBottomControlHeight, false);
  renderer.line.render(drawerX, barY, drawerX + drawerWidth, barY, true);
  renderer.line.render(drawerX, barY + kFrontlightRowHeight, drawerX + drawerWidth,
                       barY + kFrontlightRowHeight, true, LineRender::Style::Dotted);

  for (int control = 0; control < 2; ++control) {
    const int controlX = drawerX;
    const int controlRight = drawerX + drawerWidth;
    const int centerY = barY + control * kFrontlightRowHeight + kFrontlightRowHeight / 2;
    const int iconX = controlX + kFrontlightControlPadding;
    const int iconY = centerY - kFrontlightIconSize / 2;
    const int trackLeft = iconX + kFrontlightIconSize + kFrontlightCaretGap + kFrontlightCaretSize +
                          kFrontlightCaretGap;
    const int trackRight = controlRight - kFrontlightControlPadding - kFrontlightCaretSize - kFrontlightCaretGap;
    const int trackWidth = std::max(1, trackRight - trackLeft);
    const int value = control == 0 ? frontlight.brightness() : frontlight.colorTemperature();
    const float fraction = control == 0 ? frontlight_ui::fractionFromPercent(value)
                                       : static_cast<float>(value) / 100.0f;
    const int markerX = trackLeft + static_cast<int>(trackWidth * fraction + 0.5f);

    if (control == 0) {
      renderer.bitmap.icon(frontlight.brightness() > 0 ? LightOn : LightOff, iconX, iconY,
                           kFrontlightIconSize, kFrontlightIconSize);
    } else {
      renderer.bitmap.icon(Temp, iconX, iconY, kFrontlightIconSize, kFrontlightIconSize);
    }
    renderer.bitmap.icon(LibraryFilterLeft, trackLeft - kFrontlightCaretGap - kFrontlightCaretSize,
                         centerY - kFrontlightCaretSize / 2, kFrontlightCaretSize, kFrontlightCaretSize);
    renderer.bitmap.icon(LibraryFilterRight, trackRight + kFrontlightCaretGap,
                         centerY - kFrontlightCaretSize / 2, kFrontlightCaretSize, kFrontlightCaretSize);
    renderer.rectangle.fill(trackLeft, centerY - kFrontlightTrackHeight / 2, trackWidth,
                            kFrontlightTrackHeight, true, true);
    renderer.circle.render(markerX, centerY, kFrontlightKnobRadius, true);
    renderer.circle.render(markerX, centerY, kFrontlightKnobRadius - kFrontlightKnobBorder, false);
  }
#endif
}

bool SettingsDrawer::handleFrontlightInput(MappedInputManager& input, const uint32_t currentTime) {
#if FREEINK_DEVICE_X4PRO
  if (embedded_ || !frontlight.present()) return false;

  const int barY = renderer.getScreenHeight() - kBottomControlHeight;
  const int barBottom = renderer.getScreenHeight();
  const auto applyPosition = [&](const int x) {
    const int controlX = drawerX;
    const int controlRight = drawerX + drawerWidth;
    const int iconX = controlX + kFrontlightControlPadding;
    const int trackLeft = iconX + kFrontlightIconSize + kFrontlightCaretGap + kFrontlightCaretSize +
                          kFrontlightCaretGap;
    const int trackRight = controlRight - kFrontlightControlPadding - kFrontlightCaretSize - kFrontlightCaretGap;
    const int value = frontlightPercentFromTrack(x, trackLeft, std::max(1, trackRight - trackLeft),
                                                 frontlightDragControl_ == 0);
    if (frontlightDragControl_ == 0) {
      if (value == frontlight.brightness()) return false;
      frontlight.setBrightness(static_cast<uint8_t>(value));
    } else {
      if (value == frontlight.colorTemperature()) return false;
      frontlight.setColorTemperature(static_cast<uint8_t>(value));
    }
    frontlightDragChanged_ = true;
    return true;
  };
  const auto renderChange = [&](const bool changed) {
    if (!changed) return;
    lastInputTime = currentTime;
    renderWithRefresh(HalDisplay::FAST_REFRESH);
  };
  const auto commit = [&]() {
    const bool changed = frontlightDragChanged_;
    frontlightDragging_ = false;
    frontlightDragChanged_ = false;
    frontlightCaretPressed_ = false;
    if (changed) frontlight_ui::persist();
  };

  if (frontlightCaretPressed_) {
    if (input.isTouchPressed()) return true;
    commit();
    return true;
  }

  if (frontlightDragging_) {
    if (input.isTouchPressed()) {
      float nx = 0.0f;
      float ny = 0.0f;
      if (input.isTouchHeldInScreen(renderer, nx, ny)) {
        renderChange(applyPosition(static_cast<int>(nx * renderer.getScreenWidth())));
      }
      return true;
    }
    commit();
    return true;
  }

  float nx = 0.0f;
  float ny = 0.0f;
  if (input.wasTouchPressedInScreen(renderer, nx, ny)) {
    const int x = static_cast<int>(nx * renderer.getScreenWidth());
    const int y = static_cast<int>(ny * renderer.getScreenHeight());
    if (x >= drawerX && x < drawerX + drawerWidth && y >= barY && y < barBottom) {
      frontlightDragControl_ = y < barY + kFrontlightRowHeight ? 0 : 1;
      const int centerY = barY + frontlightDragControl_ * kFrontlightRowHeight + kFrontlightRowHeight / 2;
      const int iconX = drawerX + kFrontlightControlPadding;
      const int trackLeft = iconX + kFrontlightIconSize + kFrontlightCaretGap + kFrontlightCaretSize +
                            kFrontlightCaretGap;
      const int trackRight = drawerX + drawerWidth - kFrontlightControlPadding - kFrontlightCaretSize -
                             kFrontlightCaretGap;
      const int leftCaretX = trackLeft - kFrontlightCaretGap - kFrontlightCaretSize;
      const int rightCaretX = trackRight + kFrontlightCaretGap;
      if (frontlightDragControl_ == 0 && x >= iconX && x < iconX + kFrontlightIconSize) {
        if (frontlight.brightness() > 0) {
          frontlight.off();
        } else {
          frontlight.on();
        }
        frontlightCaretPressed_ = true;
        frontlightDragChanged_ = true;
        lastInputTime = currentTime;
        renderWithRefresh(HalDisplay::FAST_REFRESH);
        return true;
      }
      if (frontlightPointInCaret(x, y, leftCaretX, centerY) ||
          frontlightPointInCaret(x, y, rightCaretX, centerY)) {
        const bool increase = frontlightPointInCaret(x, y, rightCaretX, centerY);
        const int current = frontlightDragControl_ == 0 ? frontlight.brightness() : frontlight.colorTemperature();
        const int next = frontlight_ui::stepBrightness(current, increase);
        if (frontlightDragControl_ == 0) {
          frontlight.setBrightness(static_cast<uint8_t>(next));
        } else {
          frontlight.setColorTemperature(static_cast<uint8_t>(next));
        }
        frontlightCaretPressed_ = true;
        frontlightDragChanged_ = true;
        lastInputTime = currentTime;
        renderWithRefresh(HalDisplay::FAST_REFRESH);
        return true;
      }

      frontlightDragging_ = true;
      frontlightDragChanged_ = false;
      renderChange(applyPosition(x));
      return true;
    }
  }
  return false;
#else
  (void)input;
  (void)currentTime;
  return false;
#endif
}

void SettingsDrawer::drawMenuItemRow(int visibleRow, int menuIndex) {
  if (menuIndex < 0 || menuIndex >= static_cast<int>(menuItems.size())) {
    return;
  }

  const int startY = drawerY + drawerListTop();
  const int itemY = startY + (visibleRow * itemHeight);
  const auto& entry = menuItems[static_cast<size_t>(menuIndex)];
  // Touch rows keep their current value visible; they do not retain a selection fill after a tap.
  const bool isSelected = false;

  renderer.rectangle.fill(
      drawerX, itemY, drawerWidth, itemHeight,
      isSelected ? static_cast<int>(GfxRenderer::FillTone::Ink) : static_cast<int>(GfxRenderer::FillTone::Paper));

  if (entry.item == MenuItem::Separator || entry.item == MenuItem::StatusBarSeparator ||
      entry.item == MenuItem::StatusBarFullSeparator) {
    const int textX = drawerX + 15;
    const int textY = itemY + (itemHeight - renderer.text.getLineHeight(MONTSERRAT_10_FONT_ID)) / 2;
    renderer.text.render(MONTSERRAT_10_FONT_ID, textX, textY, entry.name, isSelected ? 0 : 1);

    const char* indicator = entry.getValueText(settings);
    if (indicator && indicator[0] != '\0') {
      const int indicatorW = renderer.text.getWidth(MONTSERRAT_10_FONT_ID, indicator);
      renderer.text.render(MONTSERRAT_10_FONT_ID, drawerX + drawerWidth - indicatorW - 30, textY, indicator,
                           isSelected ? 0 : 1, EpdFontFamily::BOLD);
    }

    renderer.line.render(drawerX, itemY + itemHeight - 1, drawerX + drawerWidth, itemY + itemHeight - 1, true,
                         LineRender::Style::Dotted);
    return;
  }

  const int textX = drawerX + 23;
  const int textY = itemY + (itemHeight - renderer.text.getLineHeight(MONTSERRAT_10_FONT_ID)) / 2;
  renderer.text.render(MONTSERRAT_10_FONT_ID, textX, textY, entry.name, isSelected ? 0 : 1);

  const int valueColumnRight = drawerX + drawerWidth - 24;
  const int valueAreaLeft = drawerX + drawerWidth * 40 / 100;
  if (isDropdownItem(entry.item)) {
    drawSettingsDropdown(renderer, valueAreaLeft, valueColumnRight, itemY, itemHeight, entry.getValueText(settings));
  } else if (entry.item == MenuItem::FontSize || entry.item == MenuItem::LineHeight ||
             entry.item == MenuItem::TextSpace || entry.item == MenuItem::ScreenMargin) {
    drawValueStepper(renderer, entry.getValueText(settings), valueAreaLeft, valueColumnRight, itemY, itemHeight);
  } else if (entry.item == MenuItem::Alignment) {
    int alignment = settings.paragraphAlignment;
    if (alignment < 0 || alignment > 4) alignment = 0;
    drawJustificationSegments(renderer, valueAreaLeft, valueColumnRight, itemY, itemHeight, alignment);
  } else {
    bool checkbox = false;
    bool checked = false;
    switch (entry.item) {
      case MenuItem::ExtraParagraphSpacing:
        checkbox = true;
        checked = settings.extraParagraphSpacing != 0;
        break;
      case MenuItem::ParagraphCssIndent:
        checkbox = true;
        checked = settings.paragraphCssIndentEnabled != 0;
        break;
      case MenuItem::Hyphenation:
        checkbox = true;
        checked = settings.hyphenationEnabled != 0;
        break;
      case MenuItem::BionicReading:
        checkbox = true;
        checked = settings.bionicReadingEnabled != 0;
        break;
      case MenuItem::DarkMode:
        checkbox = true;
        checked = settings.darkMode != 0;
        break;
      case MenuItem::AntiAliasing:
        checkbox = true;
        checked = settings.textAntiAliasing != 0;
        break;
      case MenuItem::ChapterSkip:
        checkbox = false;
        break;
      default:
        break;
    }
    if (checkbox) {
      ReaderFontSettingsDraw::drawToggleCheckbox(renderer, valueColumnRight, itemY, itemHeight, isSelected, checked);
    } else {
      const char* val = entry.getValueText(settings);
      if (val && val[0] != '\0') {
        const int valW = renderer.text.getWidth(MONTSERRAT_10_FONT_ID, val);
        renderer.text.render(MONTSERRAT_10_FONT_ID, valueColumnRight - valW, textY, val, isSelected ? 0 : 1);
      }
    }
  }

  renderer.line.render(drawerX, itemY + itemHeight - 1, drawerX + drawerWidth, itemY + itemHeight - 1, true,
                       LineRender::Style::Dotted);
}

bool SettingsDrawer::isDropdownItem(const MenuItem item) const {
  switch (item) {
    case MenuItem::FontFamily:
    case MenuItem::PresetPicker:
    case MenuItem::ReadingOrientation:
    case MenuItem::ReadingGuideLines:
    case MenuItem::StatusBarLeft:
    case MenuItem::StatusBarMiddle:
    case MenuItem::StatusBarRight:
    case MenuItem::StatusBarFullStyle:
      return true;
    default:
      return false;
  }
}

bool SettingsDrawer::selectorOpensUpward() const {
  return embedded_ && selectorMenuIndex_ >= 0 && selectorMenuIndex_ < static_cast<int>(menuItems.size()) &&
         menuItems[static_cast<size_t>(selectorMenuIndex_)].item == MenuItem::StatusBarFullStyle;
}

void SettingsDrawer::openSelector(const int menuIndex) {
  if (menuIndex < 0 || menuIndex >= static_cast<int>(menuItems.size()) ||
      !isDropdownItem(menuItems[static_cast<size_t>(menuIndex)].item)) {
    return;
  }

  // A selector is transient. Clear any prior selector state before building
  // the next tab's option list.
  closeSelector();
  selectorOptions_.clear();
  const MenuItem item = menuItems[static_cast<size_t>(menuIndex)].item;
  int current = 0;
  if (item == MenuItem::FontFamily) {
    selectorOptions_ = FontManager::readerFontFamilyEnumLabels();
    current = settings.fontFamily;
  } else if (item == MenuItem::PresetPicker) {
    const int count = READER_PRESETS.count();
    selectorOptions_.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
      selectorOptions_.emplace_back(READER_PRESETS.nameOf(i));
    }
    current = settings.readerPresetIndex == BookSettings::kNoReaderPreset ? 0 : settings.readerPresetIndex;
  } else if (item == MenuItem::ReadingOrientation) {
    selectorOptions_ = {"Portrait", "Landscape CW", "Inverted", "Landscape CCW"};
    current = settings.orientation;
  } else if (item == MenuItem::ReadingGuideLines) {
    selectorOptions_ = {"Off", "Grid", "Notebook"};
    current = settings.readingGuideLinesEnabled;
  } else if (item == MenuItem::StatusBarLeft || item == MenuItem::StatusBarMiddle || item == MenuItem::StatusBarRight) {
    for (int i = 0; i < static_cast<int>(StatusBarItem::STATUS_BAR_ITEM_COUNT); ++i) {
      selectorOptions_.emplace_back(statusBarItemName(static_cast<StatusBarItem>(i)));
    }
    if (item == MenuItem::StatusBarLeft) {
      current = static_cast<int>(settings.statusBarLeft.item);
    } else if (item == MenuItem::StatusBarMiddle) {
      current = static_cast<int>(settings.statusBarMiddle.item);
    } else {
      current = static_cast<int>(settings.statusBarRight.item);
    }
  } else if (item == MenuItem::StatusBarFullStyle) {
    for (int i = 0; i < StatusBar::kFullBarStyleCount; ++i) {
      selectorOptions_.emplace_back(statusBarItemName(StatusBar::kFullBarStyles[i]));
      if (StatusBar::kFullBarStyles[i] == static_cast<StatusBarItem>(settings.statusBarFullStyle)) {
        current = i;
      }
    }
  }

  if (selectorOptions_.empty()) return;
  selectorMenuIndex_ = menuIndex;
  selectorSelected_ = std::max(0, std::min(current, static_cast<int>(selectorOptions_.size()) - 1));
  selectorGroup_ = selectedGroup_;
  const int requestedRows = std::min(selectorRows, static_cast<int>(selectorOptions_.size()));
  const int selectedRow = selectorMenuIndex_ - scrollOffset;
  const int fieldY = drawerY + drawerListTop() + selectedRow * itemHeight;
  const SelectorBounds box =
      selectorBounds(drawerX, drawerWidth, fieldY, itemHeight, renderer.getScreenHeight(), requestedRows,
                     selectorOpensUpward());
  // Keep the selected option in the last visible row when opening a list.
  selectorScroll_ = std::max(0, selectorSelected_ - (box.rows - 1));
  selectorOpen_ = true;
  captureSelectorFrame();
}

void SettingsDrawer::closeSelector() {
  restoreSelectorFrame();
  selectorOpen_ = false;
  selectorMenuIndex_ = -1;
  selectorSelected_ = 0;
  selectorScroll_ = 0;
  selectorOptions_.clear();
}

void SettingsDrawer::captureSelectorFrame() {
  if (!selectorOpen_ || selectorOptions_.empty()) return;

  clearSelectorFrame();
  renderer.syncWriteBufferFromActive();
  const size_t size = renderer.getBufferSize();
  uint8_t* frame = renderer.getFrameBuffer();
  if (size == 0 || !frame) return;

  selectorFrame_ = allocateSelectorFrame(size);
  if (!selectorFrame_) return;
  std::memcpy(selectorFrame_, frame, size);
  selectorFrameSize_ = size;
}

void SettingsDrawer::restoreSelectorFrame() {
  if (selectorFrame_ && selectorFrameSize_ == renderer.getBufferSize()) {
    if (uint8_t* frame = renderer.getFrameBuffer()) {
      std::memcpy(frame, selectorFrame_, selectorFrameSize_);
    }
  }
  clearSelectorFrame();
}

void SettingsDrawer::clearSelectorFrame() {
  freeSelectorFrame(selectorFrame_);
  selectorFrame_ = nullptr;
  selectorFrameSize_ = 0;
}

void SettingsDrawer::commitSelectorSelection() {
  if (!selectorOpen_ || selectorMenuIndex_ < 0 || selectorMenuIndex_ >= static_cast<int>(menuItems.size()) ||
      selectorSelected_ < 0 || selectorSelected_ >= static_cast<int>(selectorOptions_.size())) {
    closeSelector();
    return;
  }

  const MenuItem item = menuItems[static_cast<size_t>(selectorMenuIndex_)].item;
  if (item == MenuItem::FontFamily) {
    settings.fontFamily = static_cast<uint8_t>(selectorSelected_);
    FontManager::clampReaderFontFamilySlot(settings.fontFamily);
    settings.markCustomSettings();
    settingsUpdated = true;
  } else if (item == MenuItem::PresetPicker) {
    READER_PRESETS.applyToBook(selectorSelected_, settings);
    settingsUpdated = true;
    setupMenu();
  } else if (item == MenuItem::ReadingOrientation) {
    settings.orientation = static_cast<uint8_t>(selectorSelected_);
    settings.markCustomSettings();
  } else if (item == MenuItem::ReadingGuideLines) {
    settings.readingGuideLinesEnabled = static_cast<uint8_t>(selectorSelected_);
    settings.markCustomSettings();
  } else if (item == MenuItem::StatusBarLeft) {
    settings.statusBarLeft.item = static_cast<StatusBarItem>(selectorSelected_);
    settings.markCustomSettings();
    settingsUpdated = true;
  } else if (item == MenuItem::StatusBarMiddle) {
    settings.statusBarMiddle.item = static_cast<StatusBarItem>(selectorSelected_);
    settings.markCustomSettings();
    settingsUpdated = true;
  } else if (item == MenuItem::StatusBarRight) {
    settings.statusBarRight.item = static_cast<StatusBarItem>(selectorSelected_);
    settings.markCustomSettings();
    settingsUpdated = true;
  } else if (item == MenuItem::StatusBarFullStyle) {
    settings.statusBarFullStyle = static_cast<uint8_t>(StatusBar::kFullBarStyles[selectorSelected_]);
    settings.markCustomSettings();
    settingsUpdated = true;
  }
  closeSelector();
  if (onSettingsChanged) onSettingsChanged();
}

void SettingsDrawer::drawSelectorPopup() {
  if (!selectorOpen_ || selectorGroup_ != selectedGroup_ || selectorOptions_.empty() || selectorMenuIndex_ < 0 ||
      selectorMenuIndex_ >= static_cast<int>(menuItems.size())) {
    return;
  }

  const int requestedRows = std::min(selectorRows, static_cast<int>(selectorOptions_.size()));
  const int selectedRow = selectorMenuIndex_ - scrollOffset;
  const int fieldY = drawerY + drawerListTop() + selectedRow * itemHeight;
  const SelectorBounds box =
      selectorBounds(drawerX, drawerWidth, fieldY, itemHeight, renderer.getScreenHeight(), requestedRows,
                     selectorOpensUpward());

  renderer.rectangle.fill(box.x, box.y, box.width, box.height, false);

  for (int i = 0; i < box.rows; ++i) {
    const int optionIndex = selectorScroll_ + i;
    const int rowY = box.y + i * selectorRowHeight;
    if (optionIndex < static_cast<int>(selectorOptions_.size())) {
      const bool selected = optionIndex == selectorSelected_;
      if (selected) {
        renderer.rectangle.fill(box.x + 1, rowY, box.width - 2, selectorRowHeight,
                                static_cast<int>(GfxRenderer::FillTone::Ink));
      }
      const int textY = rowY + (selectorRowHeight - renderer.text.getLineHeight(MONTSERRAT_10_FONT_ID)) / 2;
      renderer.text.render(MONTSERRAT_10_FONT_ID, box.x + 12, textY,
                           selectorOptions_[static_cast<size_t>(optionIndex)].c_str(), selected ? 0 : 1);
    }
    if (i + 1 < box.rows) {
      renderer.line.render(box.x, rowY + selectorRowHeight, box.x + box.width, rowY + selectorRowHeight, true,
                           LineRender::Style::Dotted);
    }
  }

  if (static_cast<int>(selectorOptions_.size()) > box.rows) {
    const int trackX = box.x + box.width - 5;
    const int trackY = box.y + 2;
    const int trackHeight = std::max(1, box.height - 5);
    const int totalOptions = static_cast<int>(selectorOptions_.size());
    const int thumbHeight = std::max(8, trackHeight * box.rows / totalOptions);
    const int scrollRange = std::max(1, totalOptions - box.rows);
    const int thumbRange = std::max(0, trackHeight - thumbHeight);
    const int thumbY = trackY + thumbRange * selectorScroll_ / scrollRange;
    renderer.rectangle.fill(trackX, thumbY, 2, thumbHeight, true);
  }

  renderer.rectangle.render(box.x, box.y, box.width, box.height, true);
}

bool SettingsDrawer::handleSelectorInput(MappedInputManager& input) {
  if (!selectorOpen_) return false;
  if (selectorGroup_ != selectedGroup_ || selectorMenuIndex_ < 0 ||
      selectorMenuIndex_ >= static_cast<int>(menuItems.size()) || selectorOptions_.empty()) {
    closeSelector();
    renderWithRefresh(HalDisplay::FAST_REFRESH);
    return true;
  }

  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const int requestedRows = std::min(selectorRows, static_cast<int>(selectorOptions_.size()));
  const int selectedRow = selectorMenuIndex_ - scrollOffset;
  const int fieldY = drawerY + drawerListTop() + selectedRow * itemHeight;
  const SelectorBounds box =
      selectorBounds(drawerX, drawerWidth, fieldY, itemHeight, screenH, requestedRows, selectorOpensUpward());

  if (input.hasTouch() &&
      (input.wasTouchSwipeUpForRenderer(renderer) || input.wasTouchSwipeDownForRenderer(renderer))) {
    const int maxScroll = std::max(0, static_cast<int>(selectorOptions_.size()) - box.rows);
    const int page = std::max(1, box.rows);
    if (input.wasTouchSwipeUpForRenderer(renderer)) {
      selectorScroll_ = std::min(selectorScroll_ + page, maxScroll);
    } else {
      selectorScroll_ = std::max(0, selectorScroll_ - page);
    }
    renderWithRefresh(HalDisplay::FAST_REFRESH);
    return true;
  }

  float tapNx = 0.0f;
  float tapNy = 0.0f;
  if (input.hasTouch() && input.wasTouchTapInScreen(renderer, tapNx, tapNy)) {
    const int tapX = static_cast<int>(tapNx * screenW);
    const int tapY = static_cast<int>(tapNy * screenH);
    const int optionY = tapY - box.y;
    if (tapX >= box.x && tapX < box.x + box.width && optionY >= 0 && optionY < box.rows * selectorRowHeight) {
      const int optionIndex = selectorScroll_ + optionY / selectorRowHeight;
      if (optionIndex >= 0 && optionIndex < static_cast<int>(selectorOptions_.size())) {
        selectorSelected_ = optionIndex;
        commitSelectorSelection();
        renderWithRefresh(HalDisplay::FAST_REFRESH);
        return true;
      }
    }

    const int listTop = drawerY + drawerListTop();
    if (tapX >= drawerX && tapX < drawerX + drawerWidth && tapY >= drawerY && tapY < listTop) {
      static constexpr GroupType tabs[] = {
          GroupType::FONT,
          GroupType::LAYOUT,
          GroupType::STATUS_BAR,
          GroupType::CONTROLS,
      };
      const int tabWidth = std::max(1, drawerWidth / static_cast<int>(sizeof(tabs) / sizeof(tabs[0])));
      const int tab = std::min(static_cast<int>(sizeof(tabs) / sizeof(tabs[0])) - 1,
                               std::max(0, (tapX - drawerX) / tabWidth));
      closeSelector();
      selectGroup(tabs[tab]);
      renderWithRefresh(HalDisplay::FAST_REFRESH);
      return true;
    }

    closeSelector();
    renderWithRefresh(HalDisplay::FAST_REFRESH);
    return true;
  }

  if (input.wasReleased(MappedInputManager::Button::Back)) {
    closeSelector();
    renderWithRefresh(HalDisplay::FAST_REFRESH);
    return true;
  }
  if (input.wasPressed(MappedInputManager::Button::Up) || input.wasPressed(MappedInputManager::Button::Down)) {
    const int count = static_cast<int>(selectorOptions_.size());
    const int direction = input.wasPressed(MappedInputManager::Button::Up) ? -1 : 1;
    selectorSelected_ = (selectorSelected_ + direction + count) % count;
    if (selectorSelected_ < selectorScroll_) selectorScroll_ = selectorSelected_;
    if (selectorSelected_ >= selectorScroll_ + box.rows) selectorScroll_ = selectorSelected_ - box.rows + 1;
    renderWithRefresh(HalDisplay::FAST_REFRESH);
    return true;
  }
  if (input.wasPressed(MappedInputManager::Button::Confirm)) {
    commitSelectorSelection();
    renderWithRefresh(HalDisplay::FAST_REFRESH);
    return true;
  }
  return true;
}

/**
 * @brief Draws a scroll indicator when content exceeds visible area
 */
void SettingsDrawer::drawScrollIndicator() {
  int totalItems = static_cast<int>(menuItems.size());
  if (totalItems <= itemsPerPage) return;

  int startY = drawerY + drawerListTop();
  int listHeight = itemsPerPage * itemHeight;
  int thumbH = (itemsPerPage * listHeight) / totalItems;
  int thumbY = startY + (scrollOffset * listHeight) / totalItems;

  renderer.rectangle.fill(drawerX + drawerWidth - 4, thumbY, 2, thumbH, true);
}

void SettingsDrawer::clearScrollIndicatorArea() {
  const int startY = drawerY + drawerListTop();
  const int listHeight = itemsPerPage * itemHeight;
  renderer.rectangle.fill(drawerX + drawerWidth - 5, startY, 4, listHeight, false);
}

void SettingsDrawer::refreshSelectionRows(int previousIndex, bool redrawScrollIndicator) {
  (void)previousIndex;
  (void)redrawScrollIndicator;
  if (!visible) {
    return;
  }

  // A row-only partial update can erase the drawer fill/borders on the e-paper controller. Always
  // repaint the complete drawer after selection/value changes so the container remains present.
  renderWithRefresh(HalDisplay::FAST_REFRESH);
}

/**
 * @brief Toggles expansion state of a settings group
 * @param group The group to toggle
 */
void SettingsDrawer::toggleGroup(GroupType group) {
  groupExpanded_[groupIndex(group)] = !groupExpanded_[groupIndex(group)];
  setupMenu();

  for (size_t i = 0; i < menuItems.size(); i++) {
    if (menuItems[i].group == group &&
        (menuItems[i].item == MenuItem::Separator || menuItems[i].item == MenuItem::StatusBarSeparator ||
         menuItems[i].item == MenuItem::StatusBarFullSeparator)) {
      selectedIndex = static_cast<int>(i);
      if (selectedIndex < scrollOffset) {
        scrollOffset = selectedIndex;
      } else if (selectedIndex >= scrollOffset + itemsPerPage) {
        scrollOffset = selectedIndex - itemsPerPage + 1;
      }
      break;
    }
  }
}

void SettingsDrawer::selectGroup(const GroupType group) {
  if (selectedGroup_ == group) {
    return;
  }
  selectedGroup_ = group;
  selectedIndex = -1;
  scrollOffset = 0;
  closeSelector();
  setupMenu();
}

/**
 * @brief Handles input for the settings drawer
 * @param input Reference to the input manager
 */
void SettingsDrawer::handleInput(MappedInputManager& input) {
  if (!visible) return;

  uint32_t currentTime = xTaskGetTickCount();
  if (selectorOpen_) {
    handleSelectorInput(input);
    lastInputTime = currentTime;
    return;
  }

  // Consume the fixed bottom light controls before the drawer's vertical swipe handling. A horizontal
  // drag must never be interpreted as a request to page or dismiss the drawer.
  if (handleFrontlightInput(input, currentTime)) {
    return;
  }

  if (currentTime - lastInputTime < pdMS_TO_TICKS(150)) {
    return;
  }

  bool needRedraw = false;

  // Touch is the primary input on Sticky.  The drawer is shared by the in-book settings screen and
  // the embedded Reader Preset editor, so handle the same row geometry in both places.  A tap on a
  // group separator expands it; a tap on a setting selects it and advances its value once, matching
  // the left/right value action used by the button UI.
  if (input.hasTouch()) {
    if (input.wasTouchSwipeUpForRenderer(renderer) || input.wasTouchSwipeDownForRenderer(renderer)) {
      const int totalItems = static_cast<int>(menuItems.size());
      const int maxScroll = std::max(0, totalItems - itemsPerPage);
      const int page = std::max(1, itemsPerPage);
      if (input.wasTouchSwipeUpForRenderer(renderer)) {
        scrollOffset = std::min(scrollOffset + page, maxScroll);
      } else {
        scrollOffset = std::max(scrollOffset - page, 0);
      }
      selectedIndex = -1;
      lastInputTime = currentTime;
      renderWithRefresh(HalDisplay::FAST_REFRESH);
      return;
    }

    float tapNx = 0.0f;
    float tapNy = 0.0f;
    if (input.wasTouchTapInScreen(renderer, tapNx, tapNy)) {
      const int tapX = static_cast<int>(tapNx * renderer.getScreenWidth());
      const int tapY = static_cast<int>(tapNy * renderer.getScreenHeight());
      const bool insideDrawer = tapX >= drawerX && tapX < drawerX + drawerWidth && tapY >= drawerY &&
                                tapY < drawerY + drawerHeight;

      if (!insideDrawer) {
        // The standalone in-book drawer is dismissed by tapping the page outside it.  The embedded
        // preset editor owns the preview region, so leave it open when that region is tapped.
        if (!embedded_) {
          hide();
          lastInputTime = currentTime;
        }
        return;
      }

      const int listStartY = drawerY + drawerListTop();
      if (tapY >= drawerY && tapY < listStartY) {
        static constexpr GroupType tabs[] = {
            GroupType::FONT,
            GroupType::LAYOUT,
            GroupType::STATUS_BAR,
            GroupType::CONTROLS,
        };
        const int count = static_cast<int>(sizeof(tabs) / sizeof(tabs[0]));
        const int tabCount = embedded_ ? count : count + 2;
        const int tabWidth = std::max(1, drawerWidth / tabCount);
        const int tab = std::min(tabCount - 1, std::max(0, (tapX - drawerX) / tabWidth));
        if (!embedded_ && tab == count) {
          touchEnabled_ = !touchEnabled_;
          lastInputTime = currentTime;
          renderWithRefresh(HalDisplay::FAST_REFRESH);
          return;
        }
        if (!embedded_ && tab == count + 1) {
          settings.orientation = settings.orientation == SystemSetting::LANDSCAPE_CCW
                                     ? SystemSetting::PORTRAIT
                                     : SystemSetting::LANDSCAPE_CCW;
          settings.markCustomSettings();
          settingsUpdated = true;
          hide();
          lastInputTime = currentTime;
          return;
        }
        selectGroup(tabs[tab]);
        lastInputTime = currentTime;
        renderWithRefresh(HalDisplay::FAST_REFRESH);
        return;
      }
      const int visibleRow = (tapY - listStartY) / itemHeight;
      const int tappedIndex = scrollOffset + visibleRow;
      if (tapY >= listStartY && visibleRow >= 0 && visibleRow < itemsPerPage && tappedIndex >= 0 &&
          tappedIndex < static_cast<int>(menuItems.size())) {
        const int previousIndex = selectedIndex;
        selectedIndex = tappedIndex;
        const auto selectedItem = menuItems[static_cast<size_t>(selectedIndex)].item;
        if (selectedItem == MenuItem::Separator || selectedItem == MenuItem::StatusBarSeparator ||
            selectedItem == MenuItem::StatusBarFullSeparator) {
          toggleGroup(menuItems[static_cast<size_t>(selectedIndex)].group);
          lastInputTime = currentTime;
          renderWithRefresh(HalDisplay::FAST_REFRESH);
        } else if (isDropdownItem(selectedItem)) {
          openSelector(selectedIndex);
          lastInputTime = currentTime;
          renderWithRefresh(HalDisplay::FAST_REFRESH);
        } else {
          bool appliedTouchPosition = false;
          if (selectedItem == MenuItem::FontSize || selectedItem == MenuItem::LineHeight ||
              selectedItem == MenuItem::TextSpace || selectedItem == MenuItem::ScreenMargin) {
            const char* value = menuItems[static_cast<size_t>(selectedIndex)].getValueText(settings);
            constexpr int iconSize = 30;
            constexpr int gap = 8;
            const int valueAreaLeft = drawerX + drawerWidth * 40 / 100;
            const int valueColumnRight = drawerX + drawerWidth - 24;
            const int valueW = renderer.text.getWidth(MONTSERRAT_10_FONT_ID, value, EpdFontFamily::REGULAR);
            const int width = iconSize + gap + valueW + gap + iconSize;
            const int x = std::max(valueAreaLeft, valueColumnRight - width);
            const int rightX = x + iconSize + gap + valueW + gap;
            if (tapX >= x && tapX < x + iconSize) {
              applyChange(-1);
              appliedTouchPosition = true;
            } else if (tapX >= rightX && tapX < rightX + iconSize) {
              applyChange(1);
              appliedTouchPosition = true;
            }
          } else if (selectedItem == MenuItem::Alignment) {
            const int valueAreaLeft = drawerX + drawerWidth * 40 / 100;
            const int valueColumnRight = drawerX + drawerWidth - 24;
            if (tapX >= valueAreaLeft && tapX <= valueColumnRight) {
              constexpr int segmentCount = 5;
              const int segmentWidth = std::max(1, (valueColumnRight - valueAreaLeft) / segmentCount);
              const int segment = std::min(segmentCount - 1, (tapX - valueAreaLeft) / segmentWidth);
              settings.paragraphAlignment = static_cast<uint8_t>(segment);
              settings.markCustomSettings();
              settingsUpdated = true;
              appliedTouchPosition = true;
            }
          }
          if (!appliedTouchPosition) {
            applyChange(1);
          }
          lastInputTime = currentTime;
          refreshSelectionRows(previousIndex, true);
        }
        return;
      }

      // Tapping the drawer header is intentionally inert; it is a title, not a selectable row.
      return;
    }
  }

  if (readSettingsListPrev(input, renderer)) {
    const int previousIndex = selectedIndex;
    const int totalItems = static_cast<int>(menuItems.size());
    if (totalItems > 0) {
      selectedIndex = (selectedIndex - 1 + totalItems) % totalItems;
      const int maxScroll = std::max(0, totalItems - itemsPerPage);
      const bool scrolled = selectedIndex < scrollOffset || selectedIndex >= scrollOffset + itemsPerPage;
      if (selectedIndex < scrollOffset) {
        scrollOffset = selectedIndex;
      } else if (selectedIndex >= scrollOffset + itemsPerPage) {
        scrollOffset = std::min(selectedIndex - itemsPerPage + 1, maxScroll);
      }
      scrollOffset = std::max(0, std::min(scrollOffset, maxScroll));
      if (scrolled) {
        needRedraw = true;
      } else {
        lastInputTime = currentTime;
        refreshSelectionRows(previousIndex, false);
        return;
      }
    }
  }

  if (readSettingsListNext(input, renderer)) {
    const int previousIndex = selectedIndex;
    const int totalItems = static_cast<int>(menuItems.size());
    if (totalItems > 0) {
      selectedIndex = (selectedIndex + 1) % totalItems;
      const int maxScroll = std::max(0, totalItems - itemsPerPage);
      const bool scrolled = selectedIndex < scrollOffset || selectedIndex >= scrollOffset + itemsPerPage;
      if (selectedIndex < scrollOffset) {
        scrollOffset = selectedIndex;
      } else if (selectedIndex >= scrollOffset + itemsPerPage) {
        scrollOffset = std::min(selectedIndex - itemsPerPage + 1, maxScroll);
      }
      scrollOffset = std::max(0, std::min(scrollOffset, maxScroll));
      if (scrolled) {
        needRedraw = true;
      } else {
        lastInputTime = currentTime;
        refreshSelectionRows(previousIndex, false);
        return;
      }
    }
  }

  if (readValueDecrease(input, renderer)) {
    applyChange(-1);
    needRedraw = true;
  }

  if (readValueIncrease(input, renderer)) {
    applyChange(1);
    needRedraw = true;
  }

  if (input.wasPressed(MappedInputManager::Button::Confirm)) {
    if (selectedIndex >= 0 && selectedIndex < static_cast<int>(menuItems.size())) {
      const auto& selected = menuItems[selectedIndex];
      if (selected.item == MenuItem::Separator || selected.item == MenuItem::StatusBarSeparator ||
          selected.item == MenuItem::StatusBarFullSeparator) {
        toggleGroup(selected.group);
        needRedraw = true;
      } else if (isDropdownItem(selected.item)) {
        openSelector(selectedIndex);
        needRedraw = true;
      }
    }
  }

  if (input.wasReleased(MappedInputManager::Button::Back)) {
    hide();
    needRedraw = true;
  }

  if (needRedraw) {
    lastInputTime = currentTime;
    renderWithRefresh(HalDisplay::FAST_REFRESH);
  }
}

/**
 * @brief Applies a delta change to the currently selected menu item
 * @param delta Amount to change (-1 or 1)
 */
void SettingsDrawer::applyChange(int delta) {
  if (selectedIndex < 0 || selectedIndex >= static_cast<int>(menuItems.size())) return;
  const MenuItem selectedItem = menuItems[selectedIndex].item;
  menuItems[selectedIndex].change(settings, delta);

  if (selectedItem == MenuItem::PresetPicker) {
    settingsUpdated = true;
    setupMenu();
    if (selectedIndex >= static_cast<int>(menuItems.size())) {
      selectedIndex = std::max(0, static_cast<int>(menuItems.size()) - 1);
    }
  } else {
    switch (selectedItem) {
      case MenuItem::FontSize:
      case MenuItem::LineHeight:
      case MenuItem::TextSpace:
      case MenuItem::ScreenMargin:
      case MenuItem::Alignment:
      case MenuItem::ExtraParagraphSpacing:
      case MenuItem::ParagraphCssIndent:
      case MenuItem::BionicReading:
      case MenuItem::Hyphenation:
      case MenuItem::FontFamily:
        settingsUpdated = true;
        break;
      case MenuItem::ReadingOrientation:
      case MenuItem::PageAutoTurn:
      case MenuItem::ReaderImageGrayscale:
      case MenuItem::ReaderPowerButton:
      case MenuItem::DarkMode:
      // Pure visual overlay — never affects text layout/pagination, so it never needs the expensive
      // full-page rebuild that settingsUpdated triggers, just the normal redraw that already happens.
      case MenuItem::ReadingGuideLines:
        break;
      case MenuItem::StatusBarLeft:
      case MenuItem::StatusBarMiddle:
      case MenuItem::StatusBarRight:
      case MenuItem::StatusBarFullStyle:
        settingsUpdated = true;
        break;
      case MenuItem::RefreshRate:
      case MenuItem::AntiAliasing:
      case MenuItem::ChapterSkip:
      case MenuItem::NavigationLock:
      case MenuItem::Separator:
      case MenuItem::StatusBarSeparator:
      case MenuItem::StatusBarFullSeparator:
      case MenuItem::PresetPicker:
        break;
    }
  }

  if (selectedItem != MenuItem::ReaderPowerButton && onSettingsChanged) onSettingsChanged();
}
