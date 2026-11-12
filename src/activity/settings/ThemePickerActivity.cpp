#include "ThemePickerActivity.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "activity/page/SubPage.h"
#include "activity/page/components/global/Button.h"
#include "activity/page/components/global/Paginator.h"
#include "activity/page/components/global/PopUp.h"
#include "activity/page/components/global/Toggle.h"
#include "activity/settings/BaseCarouselActivity.h"
#include "activity/settings/BaseHeatmapActivity.h"
#include "activity/settings/BaseLibraryActivity.h"
#include "activity/settings/BaseDescriptionActivity.h"
#include "activity/settings/BaseTemperatureActivity.h"
#include "activity/settings/SettingsListLayout.h"
#include "activity/util/KeyboardEntryActivity.h"
#include "images/LibraryFilterLeft.h"
#include "images/LibraryFilterRight.h"
#include "images/ThemeBorder.h"
#include "images/Setting.h"
#include "state/HomeTheme.h"
#include "state/SystemSetting.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "system/TemperaturePreferences.h"

namespace {

// Keep Theme's mode/theme lists aligned with the shared Settings list geometry.
constexpr int kRowHeight = SettingsListLayout::kRowHeight;
constexpr int kBottomButtonMargin = 20;
// SubPage::header leaves a 20pt gap below the header icon before page content.
constexpr int kThemeContentTop = navigation::Menu::topPadding + navigation::Menu::iconSize + 20;

struct PreviewBounds {
  int x;
  int y;
  int width;
  int height;
};

PreviewBounds widgetPreviewBounds(const GfxRenderer& renderer) {
  const int contentTop = kThemeContentTop;
  const int previewHeight = std::max(1, renderer.getScreenHeight() - contentTop - 20);
  return {0, contentTop, renderer.getScreenWidth(), previewHeight};
}

ButtonBounds newThemeButtonBounds(const GfxRenderer& renderer, const int font) {
  constexpr const char* label = "New theme +";
  const int width = Button::width(renderer, label, font);
  const int y = renderer.getScreenHeight() - navigation::Menu::bottomInset() - kBottomButtonMargin - Button::height;
  return {renderer.getScreenWidth() - 20 - width, y, width, Button::height};
}

int layoutIndex(const HomeTheme::Layout layout) {
  switch (layout) {
    case HomeTheme::Layout::OneByOne:
      return 0;
    case HomeTheme::Layout::OneByTwo:
      return 1;
    case HomeTheme::Layout::OneByThree:
      return 2;
    default:
      return 1;
  }
}

HomeTheme::Layout layoutAtIndex(const int index) {
  switch (index) {
    case 0:
      return HomeTheme::Layout::OneByOne;
    case 2:
      return HomeTheme::Layout::OneByThree;
    case 1:
    default:
      return HomeTheme::Layout::OneByTwo;
  }
}

bool supportsCarouselSettings(const HomeTheme::Widget widget) {
  return widget == HomeTheme::Widget::Carousel || widget == HomeTheme::Widget::Recent ||
         widget == HomeTheme::Widget::Favorites;
}

bool supportsHeatmapSettings(const HomeTheme::Widget widget) {
  (void)widget;
  return false;
}

bool supportsLibrarySettings(const HomeTheme::Widget widget) { return widget == HomeTheme::Widget::Library; }

bool supportsDescriptionSettings(const HomeTheme::Widget widget) {
  return widget == HomeTheme::Widget::Description;
}

bool supportsStatisticsSettings(const HomeTheme::Widget widget) {
  return widget == HomeTheme::Widget::Statistics;
}

bool supportsStreaksSettings(const HomeTheme::Widget widget) {
  return widget == HomeTheme::Widget::Streaks;
}

#if FREEINK_DEVICE_STICKY
bool supportsTemperatureSettings(const HomeTheme::Widget widget) {
  return widget == HomeTheme::Widget::Temperature;
}
#endif

struct CarouselSettingsBounds {
  int x;
  int y;
  int size;
};

CarouselSettingsBounds carouselSettingsBounds(const int cellX, const int cellY, const int cellW, const int cellH) {
  constexpr int size = 40;
  (void)cellW;
  return {cellX + 8, cellY + std::max(0, cellH - size - 8), size};
}

int layoutColumns(const HomeTheme::Layout layout) { return layout == HomeTheme::Layout::TwoByTwo ? 2 : 1; }

int layoutRows(const HomeTheme::Layout layout) {
  if (layout == HomeTheme::Layout::OneByOne) return 1;
  if (layout == HomeTheme::Layout::OneByThree) return 3;
  return 2;
}

void setDefaultLibraryFolders(char (&folders)[3][128]) {
  for (char (&folder)[128] : folders) {
    std::strncpy(folder, "/", sizeof(folder) - 1);
    folder[sizeof(folder) - 1] = '\0';
  }
}

int widgetOptionCount(const HomeTheme::Layout layout, const bool sleepTheme) {
  if (layout == HomeTheme::Layout::Classic) return 1;
  if (layout == HomeTheme::Layout::OneByOne && !sleepTheme) return 3;
  if (sleepTheme) {
#if FREEINK_DEVICE_STICKY
    return 8;
#else
    return 6;
#endif
  }
#if FREEINK_DEVICE_STICKY
  const int count = 19;
#else
  const int count = 17;
#endif
  return count - (layout == HomeTheme::Layout::OneByOne ? 0 : 3);
}

HomeTheme::Widget widgetOptionAt(const HomeTheme::Layout layout, const bool sleepTheme, const int index) {
  static constexpr HomeTheme::Widget oneByOneOptions[] = {
      HomeTheme::Widget::RecentGrid,
      HomeTheme::Widget::Flow,
      HomeTheme::Widget::VerticalBookshelf,
  };
  static constexpr HomeTheme::Widget homeOptions[] = {
      HomeTheme::Widget::Empty,
      HomeTheme::Widget::RecentGrid,
      HomeTheme::Widget::Flow,
      HomeTheme::Widget::VerticalBookshelf,
      HomeTheme::Widget::Carousel,
      HomeTheme::Widget::Shortcuts,
      HomeTheme::Widget::Clock,
      HomeTheme::Widget::Recent,
      HomeTheme::Widget::Description,
#if FREEINK_DEVICE_STICKY
      HomeTheme::Widget::Temperature,
#endif
      HomeTheme::Widget::Calendar,
      HomeTheme::Widget::ListShortcuts,
#if FREEINK_DEVICE_STICKY
      HomeTheme::Widget::Humidity,
#endif
    HomeTheme::Widget::TodaysReading,
    HomeTheme::Widget::Favorites,
    HomeTheme::Widget::Heatmap,
    HomeTheme::Widget::Library,
    HomeTheme::Widget::Statistics,
    HomeTheme::Widget::Streaks,
  };
  if (layout == HomeTheme::Layout::OneByOne && !sleepTheme) {
    if (index < 0 || index >= static_cast<int>(sizeof(oneByOneOptions) / sizeof(oneByOneOptions[0]))) {
      return HomeTheme::Widget::Empty;
    }
    return oneByOneOptions[index];
  }
  static constexpr HomeTheme::Widget sleepOptions[] = {
      HomeTheme::Widget::Empty,
      HomeTheme::Widget::Clock,
      HomeTheme::Widget::Calendar,
#if FREEINK_DEVICE_STICKY
      HomeTheme::Widget::Temperature,
      HomeTheme::Widget::Humidity,
#endif
      HomeTheme::Widget::Heatmap,
      HomeTheme::Widget::Statistics,
      HomeTheme::Widget::Streaks,
  };
  const HomeTheme::Widget* options = sleepTheme ? sleepOptions : homeOptions;
  const int optionCount = sleepTheme ? static_cast<int>(sizeof(sleepOptions) / sizeof(sleepOptions[0]))
                                     : static_cast<int>(sizeof(homeOptions) / sizeof(homeOptions[0]));
  if (index < 0 || index >= widgetOptionCount(layout, sleepTheme)) return HomeTheme::Widget::Empty;
  int visible = 0;
  for (int option = 0; option < optionCount; ++option) {
    if (layout != HomeTheme::Layout::OneByOne &&
        (options[option] == HomeTheme::Widget::RecentGrid || options[option] == HomeTheme::Widget::Flow ||
         options[option] == HomeTheme::Widget::VerticalBookshelf)) {
      continue;
    }
    if (visible++ == index) return options[option];
  }
  return HomeTheme::Widget::Empty;
}

int widgetOptionIndex(const HomeTheme::Layout layout, const bool sleepTheme, const HomeTheme::Widget widget) {
  const int count = widgetOptionCount(layout, sleepTheme);
  for (int i = 0; i < count; ++i) {
    if (widgetOptionAt(layout, sleepTheme, i) == widget) return i;
  }
  return 0;
}

int oneByOneWidgetCount(const HomeTheme::Widget* widgets) {
  int count = 0;
  for (int slot = 0; slot < HomeTheme::slotCount(HomeTheme::Layout::OneByOne); ++slot) {
    if (widgets[slot] != HomeTheme::Widget::Empty) ++count;
  }
  return count;
}

int oneByOneSlotAtPage(const HomeTheme::Widget* widgets, int page) {
  const int count = oneByOneWidgetCount(widgets);
  if (count <= 0) return 0;
  page = (page % count + count) % count;
  for (int slot = 0; slot < HomeTheme::slotCount(HomeTheme::Layout::OneByOne); ++slot) {
    if (widgets[slot] == HomeTheme::Widget::Empty) continue;
    if (page-- == 0) return slot;
  }
  return 0;
}

struct BorderIconBounds {
  int x;
  int y;
  int size;
};

BorderIconBounds borderIconBounds(const int cellX, const int cellY, const int cellW, const int cellH) {
  constexpr int size = 40;
  const int dividerY = cellY + cellH;
  return {cellX + std::max(0, cellW - size - 6), dividerY - size / 2, size};
}

bool inside(const int x, const int y, const int left, const int top, const int width, const int height) {
  return x >= left && x < left + width && y >= top && y < top + height;
}

constexpr int kWidgetGridColumns = 1;
constexpr int kWidgetGridRows = 2;
constexpr int kWidgetGridPageSize = kWidgetGridColumns * kWidgetGridRows;

struct WidgetGridBounds {
  int x;
  int y;
  int width;
  int height;
  int header;
  int gridX;
  int gridY;
  int cellWidth;
  int cellHeight;
};

WidgetGridBounds widgetGridBounds(const GfxRenderer& renderer) {
  constexpr int padding = 20;
  constexpr int gap = 12;
  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  const int width = screenWidth;
  const int height = screenHeight;
  const int header = (FREEINK_DEVICE_X4PRO ? 20 : 15) + 40 + 20;
  const int x = 0;
  const int y = 0;
  const int gridX = x;
  const int gridY = y + header + padding;
  const int gridWidth = std::max(kWidgetGridColumns, width - 16);
  const int gridHeight = std::max(kWidgetGridRows, height - header - padding * 2);
  const int horizontalGap = kWidgetGridColumns > 1 ? gap : 0;
  const int cellWidth = std::max(1, (gridWidth - horizontalGap) / kWidgetGridColumns);
  const int cellHeight = std::max(1, (gridHeight - gap) / kWidgetGridRows);
  return {x, y, width, height, header, gridX, gridY, cellWidth, cellHeight};
}

void drawLibraryPreviewPlaceholders(GfxRenderer& renderer, const int x, const int y, const int width, const int height,
                                    const bool background, const bool showLabel, const bool oneByThree) {
  constexpr int padding = 20;
  constexpr int gap = 20;
  constexpr int layerStep = 7;
  constexpr int count = LibraryWidget::kVisibleFolderCount;
  if (background) {
    for (int py = (y + 1) & ~1; py < y + height; py += 2) {
      for (int px = (x + 1) & ~1; px < x + width; px += 2) renderer.drawPixel(px, py, true);
    }
  } else {
    renderer.rectangle.fill(x, y, width, height, false);
  }
  const int contentX = x + padding;
  const int contentY = y + padding;
  const int contentWidth = std::max(8, width - padding * 2);
  const int contentHeight = std::max(8, height - padding * 2);
  const int cellWidth = std::max(8, (contentWidth - gap) / count);
  const int labelFont = systemFontId();
  const int labelHeight = showLabel ? renderer.text.getLineHeight(labelFont) : 0;
  const int labelSpacing = showLabel ? 14 : 0;
  const int availableImageHeight = std::max(8, contentHeight - labelHeight - 10 - labelSpacing);
  const int imageHeight = std::max(8, availableImageHeight * 2 / 3);
  const int cellInset = oneByThree ? 0 : 5;
  for (int index = 0; index < count; ++index) {
    const int cellX = contentX + index * (cellWidth + gap);
    const int availableWidth = std::max(24, cellWidth - cellInset * 2);
    const int frontWidth = std::max(12, std::min(availableWidth - layerStep * 3, imageHeight * 2 / 3));
    const int frontHeight = imageHeight;
    const int stackWidth = frontWidth + layerStep * 3;
    const int stackX = cellX + std::max(cellInset, (cellWidth - stackWidth) / 2);
    const int frontX = stackX + layerStep * 3;
    const int frontY = contentY + cellInset + (availableImageHeight - imageHeight) / 2;
    const int backingHeight = std::max(8, imageHeight - 18);
    const int thirdHeight = std::max(8, imageHeight - 12);
    const int secondHeight = std::max(8, imageHeight - 6);
    const int backingY = frontY + (imageHeight - backingHeight) / 2;
    const int thirdY = frontY + (imageHeight - thirdHeight) / 2;
    const int secondY = frontY + (imageHeight - secondHeight) / 2;
    renderer.rectangle.fill(frontX - layerStep * 2, backingY, frontWidth, backingHeight, false);
    renderer.rectangle.render(frontX - layerStep * 2, backingY, frontWidth, backingHeight, true);
    renderer.rectangle.fill(frontX - layerStep * 2, thirdY, layerStep * 2, thirdHeight, false);
    renderer.rectangle.render(frontX - layerStep * 2, thirdY, layerStep * 2, thirdHeight, true);
    renderer.rectangle.fill(frontX - layerStep, secondY, layerStep, secondHeight, false);
    renderer.rectangle.render(frontX - layerStep, secondY, layerStep, secondHeight, true);
    renderer.rectangle.fill(frontX, frontY, frontWidth, frontHeight, false);
    renderer.rectangle.render(frontX, frontY, frontWidth, frontHeight, true);
    if (showLabel) {
      const std::string label = "Folder " + std::to_string(index + 1);
      const std::string shown = renderer.text.truncate(labelFont, label.c_str(), frontWidth);
      renderer.text.render(labelFont, frontX, contentY + contentHeight - labelHeight - 10, shown.c_str(), true,
                           EpdFontFamily::REGULAR);
    }
  }
}

}

void ThemePickerActivity::onEnter() {
  ActivityWithSubactivity::onEnter();
  HomeTheme::load();
  sleepMode_ = openSleepTheme_;
  selected_ = sleepMode_ ? HomeTheme::sleepActiveIndex() : HomeTheme::activeIndex();
  listCursor_ = selected_ + 1;
  screen_ = sleepMode_ ? Screen::List : Screen::Mode;
  render();
}

void ThemePickerActivity::close() {
  if (onBack_) onBack_();
}

void ThemePickerActivity::render() {
  renderer.clearScreen();
  if (screen_ == Screen::Mode) {
    renderMode();
  } else if (screen_ == Screen::List) {
    renderList();
  } else if (screen_ == Screen::Layouts) {
    renderLayoutPicker();
  } else if (!widgetPopup_) {
    renderWidgetPicker();
  }
  if (widgetPopup_) {
    renderWidgetPopup();
  }
  if (borderPopup_) {
    renderBorderPopup();
  }
  if (themeActionPopup_) {
    renderThemeActionPopup();
  }
  renderer.displayBuffer();
}

void ThemePickerActivity::renderMode() {
  const int contentTop = SubPage::header(renderer, "Theme");
  const int font = systemFontId();
  const int width = renderer.getScreenWidth();
  const char* labels[] = {"Home", "Sleep"};
  for (int index = 0; index < 2; ++index) {
    const int y = contentTop + index * kRowHeight;
    renderer.rectangle.fill(0, y, width, kRowHeight, static_cast<int>(GfxRenderer::FillTone::Paper));
    renderer.text.render(font, 20, y + (kRowHeight - renderer.text.getLineHeight(font)) / 2, labels[index], true,
                         EpdFontFamily::REGULAR);
    if (index == 0) {
      renderer.line.render(SettingsListLayout::kDividerInset, y + kRowHeight - 1,
                           width - SettingsListLayout::kDividerInset, y + kRowHeight - 1, true,
                           LineRender::Style::Dotted);
    }
  }
}

void ThemePickerActivity::renderList() {
  const char* subtitle = sleepMode_ ? "Sleep" : "Home";
  const int contentTop = SubPage::header(renderer, "Theme", subtitle);
  const int titleFont = systemFontIdForPointSize(16);
  const int font = systemFontId();
  const int width = renderer.getScreenWidth();
  const int themeCount = sleepMode_ ? HomeTheme::sleepCount() : HomeTheme::count();
  constexpr const char* newThemeLabel = "New theme +";
  const ButtonBounds button = newThemeButtonBounds(renderer, font);
  Button::render(renderer, button, newThemeLabel, false, font);

  const int themeListTop = contentTop;
  for (int i = 0; i < themeCount; ++i) {
    const int y = themeListTop + i * kRowHeight;
    const HomeTheme::Theme& theme = sleepMode_ ? HomeTheme::sleepAt(i) : HomeTheme::at(i);
    renderer.rectangle.fill(0, y, width, kRowHeight, static_cast<int>(GfxRenderer::FillTone::Paper));
    renderer.text.render(font, 20, y + (kRowHeight - renderer.text.getLineHeight(font)) / 2,
                         theme.name, true, EpdFontFamily::REGULAR);
    const char* layout = HomeTheme::layoutLabel(theme.layout);
    const int layoutX = width - renderer.text.getWidth(font, layout) - 84;
    renderer.text.render(font, layoutX, y + (kRowHeight - renderer.text.getLineHeight(font)) / 2, layout,
                         true, EpdFontFamily::REGULAR);
    const int activeIndex = sleepMode_ ? HomeTheme::sleepActiveIndex() : HomeTheme::activeIndex();
    Toggle::render(renderer, width - 24, y, kRowHeight, activeIndex == i, false);
    if (i + 1 < themeCount) {
      renderer.line.render(SettingsListLayout::kDividerInset, y + kRowHeight - 1,
                           width - SettingsListLayout::kDividerInset, y + kRowHeight - 1, true,
                           LineRender::Style::Dotted);
    }
  }
}

void ThemePickerActivity::renderLayoutPicker() {
  const int contentTop = SubPage::header(renderer, "Choose layout");
  const int font = systemFontId();
  const int width = renderer.getScreenWidth();
  const int availableHeight = std::max(1, renderer.getScreenHeight() - contentTop);
  const int sectionHeight = availableHeight / 3;
  const int previewWidth = std::min(150, width / 3);
  for (int index = 0; index < 3; ++index) {
    const int y = contentTop + index * sectionHeight;
    const int previewHeight = std::max(40, sectionHeight - 28);
    const int previewX = 20;
    const int previewY = y + (sectionHeight - previewHeight) / 2;
    const int rows = index + 1;
    const int gap = 6;
    const int rowHeight = (previewHeight - gap * (rows - 1)) / rows;
    renderer.rectangle.fill(0, y, width, sectionHeight, static_cast<int>(GfxRenderer::FillTone::Paper));
    for (int row = 0; row < rows; ++row) {
      const int boxY = previewY + row * (rowHeight + gap);
      renderer.rectangle.fill(previewX, boxY, previewWidth, rowHeight,
                              static_cast<int>(GfxRenderer::FillTone::Paper));
      renderer.rectangle.render(previewX, boxY, previewWidth, rowHeight, true);
    }
    const char* label = HomeTheme::layoutLabel(layoutAtIndex(index));
    const int labelWidth = renderer.text.getWidth(font, label);
    renderer.text.render(font, width - labelWidth - 20,
                         y + (sectionHeight - renderer.text.getLineHeight(font)) / 2, label, true,
                         EpdFontFamily::REGULAR);
    if (index < 2) {
      renderer.line.render(SettingsListLayout::kDividerInset, y + sectionHeight - 1,
                           width - SettingsListLayout::kDividerInset, y + sectionHeight - 1, true,
                           LineRender::Style::Dotted);
    }
  }
}

void ThemePickerActivity::renderThemeActionPopup() {
  static const char* const labels[] = {"Rename", "Edit", "Delete"};
  std::vector<std::string> values;
  for (const char* label : labels) values.emplace_back(label);
  const PopUpBounds box = PopUp::bounds(renderer, static_cast<int>(values.size()));
  PopUp::background(renderer, box);
  PopUp::title(renderer, box, "Theme");
  PopUp::list(renderer, box, values, themeActionSelected_, 0);
  PopUp::border(renderer, box);
}

void ThemePickerActivity::renderWidgetPicker() {
  SubPage::header(renderer, "Layout Builder");
  const PreviewBounds preview = widgetPreviewBounds(renderer);
  const int previewX = preview.x;
  const int previewY = preview.y;
  const int previewW = preview.width;
  const int previewH = preview.height;
  renderer.rectangle.fill(previewX, previewY, previewW, previewH, false);

  const int layoutX = previewX;
  const int layoutY = previewY;
  const int layoutW = previewW;
  const int layoutH = previewH;
  const int columns = layoutColumns(layout_);
  const int rows = layoutRows(layout_);
  const int cellW = layoutW / columns;
  const int cellH = layoutH / rows;
  const int widgetPageCount = layout_ == HomeTheme::Layout::OneByOne ? oneByOneWidgetCount(widgets_) : 0;
  const int widgetPage = widgetPageCount <= 0 ? 0 : std::min(widgetPageCount - 1, std::max(0, widgetPageIndex_));
  const int firstSlot = layout_ == HomeTheme::Layout::OneByOne
                            ? oneByOneSlotAtPage(widgets_, widgetPage)
                            : 0;
  const int pageCount = layout_ == HomeTheme::Layout::OneByOne ? widgetOptionCount(layout_, editingSleep_) : 0;
  const int page = pageCount <= 0 ? 0 : std::min(pageCount - 1,
                                                  std::max(0, widgetOptionIndex(layout_, editingSleep_, widgets_[firstSlot])));
  const int lastSlot = layout_ == HomeTheme::Layout::OneByOne ? firstSlot + 1 : HomeTheme::slotCount(layout_);
  if (layout_ == HomeTheme::Layout::OneByOne) {
    // firstSlot is a persistent storage slot. It is never a screen row.
    renderWidgetPreview(widgets_[firstSlot], layoutX, layoutY, cellW, cellH, backgrounds_[firstSlot] != 0,
                        carouselStyles_[firstSlot], carouselLabels_[firstSlot] != 0,
                        carouselLabelColors_[firstSlot], carouselShadowStyles_[firstSlot],
                        heatmapViews_[firstSlot], libraryFolders_[firstSlot],
                        descriptionTitles_[firstSlot] != 0, descriptionAuthors_[firstSlot] != 0,
                        descriptionProgress_[firstSlot] != 0, descriptionRatings_[firstSlot] != 0,
                        recentTitles_[firstSlot] != 0, recentAuthors_[firstSlot] != 0,
                        recentProgress_[firstSlot] != 0, recentRatings_[firstSlot] != 0,
                        carouselProgress_[firstSlot] != 0, carouselFullHeight_[firstSlot] != 0,
                        statisticsSummary_[firstSlot] != 0, streakCompact_[firstSlot] != 0, false, true);
  } else {
    // Keep the established 1 x 2 preview path: two storage slots map directly
    // to the two visible rows.
    for (int slot = 0; slot < HomeTheme::slotCount(layout_); ++slot) {
      const int cellX = layoutX + (slot % columns) * cellW;
      const int cellY = layoutY + (slot / columns) * cellH;
      renderWidgetPreview(widgets_[slot], cellX, cellY, cellW, cellH, backgrounds_[slot] != 0,
                          carouselStyles_[slot], carouselLabels_[slot] != 0, carouselLabelColors_[slot],
                          carouselShadowStyles_[slot], heatmapViews_[slot], libraryFolders_[slot],
                          descriptionTitles_[slot] != 0, descriptionAuthors_[slot] != 0,
                          descriptionProgress_[slot] != 0, descriptionRatings_[slot] != 0,
                          recentTitles_[slot] != 0, recentAuthors_[slot] != 0,
                          recentProgress_[slot] != 0, recentRatings_[slot] != 0, carouselProgress_[slot] != 0,
                          carouselFullHeight_[slot] != 0, statisticsSummary_[slot] != 0, streakCompact_[slot] != 0,
                          false, true);
    }
  }

  if (layout_ == HomeTheme::Layout::OneByOne) {
    if (pageCount > 1) {
      constexpr int containerRadius = 22;
      constexpr int iconSize = 26;
      constexpr int edgeMargin = 10;
      const int centerY = layoutY + layoutH / 2;
      const auto drawCaret = [&](const int centerX, const uint8_t* icon) {
        renderer.circle.render(centerX, centerY, containerRadius, true);
        renderer.circle.render(centerX, centerY, containerRadius - 2, false);
        renderer.bitmap.icon(icon, centerX - iconSize / 2, centerY - iconSize / 2, iconSize, iconSize);
      };
      if (page > 0) drawCaret(edgeMargin + containerRadius, LibraryFilterLeft);
      if (page + 1 < pageCount) {
        drawCaret(renderer.getScreenWidth() - edgeMargin - containerRadius, LibraryFilterRight);
      }

      constexpr int bottomMargin = 12;
      constexpr int dotRadius = 6;
      constexpr int dotGap = 10;
      const int totalWidth = pageCount * dotRadius * 2 + (pageCount - 1) * dotGap;
      const int left = (renderer.getScreenWidth() - totalWidth) / 2;
      const int centerYBottom = layoutY + layoutH - bottomMargin - dotRadius;
      int cursorX = left;
      for (int index = 0; index < pageCount; ++index) {
        if (index == page) {
          renderer.circle.render(cursorX + dotRadius, centerYBottom, dotRadius, true);
        } else {
          renderer.circle.render(cursorX + dotRadius, centerYBottom, dotRadius, true);
          renderer.circle.render(cursorX + dotRadius, centerYBottom, dotRadius - 2, false);
        }
        cursorX += dotRadius * 2;
        if (index + 1 < pageCount) cursorX += dotGap;
      }
    }
  }

  if (layout_ == HomeTheme::Layout::OneByThree) {
    renderDividerBorder(borders_[0], layoutX, layoutY + cellH, layoutW);
    renderDividerBorder(borders_[1], layoutX, layoutY + cellH * 2, layoutW);
  } else {
    renderBorder(borders_[firstSlot], layoutX, layoutY, layoutW, layoutH);
  }
  if (layout_ != HomeTheme::Layout::OneByOne) {
    const int dividerCount = layout_ == HomeTheme::Layout::OneByThree ? 2 : 1;
    for (int divider = 0; divider < dividerCount; ++divider) {
      const BorderIconBounds icon = borderIconBounds(layoutX, layoutY + divider * cellH, layoutW, cellH);
      renderer.rectangle.fill(icon.x - 3, icon.y - 3, icon.size + 6, icon.size + 6, false);
      renderer.rectangle.render(icon.x - 3, icon.y - 3, icon.size + 6, icon.size + 6, true);
      renderer.icon.render(ThemeBorder, icon.x, icon.y, icon.size, icon.size);
    }
  }

  for (int slot = firstSlot; slot < lastSlot; ++slot) {
    const int visualSlot = layout_ == HomeTheme::Layout::OneByOne ? 0 : slot;
    const int cellX = layoutX + (visualSlot % columns) * cellW;
    const int cellY = layoutY + (visualSlot / columns) * cellH;
    if (supportsCarouselSettings(widgets_[slot])) {
      const CarouselSettingsBounds settings = carouselSettingsBounds(cellX, cellY, cellW, cellH);
      renderer.rectangle.fill(settings.x - 4, settings.y - 4, settings.size + 8, settings.size + 8, false);
      renderer.rectangle.render(settings.x - 4, settings.y - 4, settings.size + 8, settings.size + 8, true);
      renderer.bitmap.icon(Setting, settings.x, settings.y, settings.size, settings.size);
    }
    if (supportsHeatmapSettings(widgets_[slot])) {
      const CarouselSettingsBounds settings = carouselSettingsBounds(cellX, cellY, cellW, cellH);
      renderer.rectangle.fill(settings.x - 4, settings.y - 4, settings.size + 8, settings.size + 8, false);
      renderer.rectangle.render(settings.x - 4, settings.y - 4, settings.size + 8, settings.size + 8, true);
      renderer.bitmap.icon(Setting, settings.x, settings.y, settings.size, settings.size);
    }
#if FREEINK_DEVICE_STICKY
    if (supportsTemperatureSettings(widgets_[slot])) {
      const CarouselSettingsBounds settings = carouselSettingsBounds(cellX, cellY, cellW, cellH);
      renderer.rectangle.fill(settings.x - 4, settings.y - 4, settings.size + 8, settings.size + 8, false);
      renderer.rectangle.render(settings.x - 4, settings.y - 4, settings.size + 8, settings.size + 8, true);
      renderer.bitmap.icon(Setting, settings.x, settings.y, settings.size, settings.size);
    }
#endif
    if (supportsLibrarySettings(widgets_[slot])) {
      const CarouselSettingsBounds settings = carouselSettingsBounds(cellX, cellY, cellW, cellH);
      renderer.rectangle.fill(settings.x - 4, settings.y - 4, settings.size + 8, settings.size + 8, false);
      renderer.rectangle.render(settings.x - 4, settings.y - 4, settings.size + 8, settings.size + 8, true);
      renderer.bitmap.icon(Setting, settings.x, settings.y, settings.size, settings.size);
    }
    if (supportsDescriptionSettings(widgets_[slot])) {
      const CarouselSettingsBounds settings = carouselSettingsBounds(cellX, cellY, cellW, cellH);
      renderer.rectangle.fill(settings.x - 4, settings.y - 4, settings.size + 8, settings.size + 8, false);
      renderer.rectangle.render(settings.x - 4, settings.y - 4, settings.size + 8, settings.size + 8, true);
      renderer.bitmap.icon(Setting, settings.x, settings.y, settings.size, settings.size);
    }
    if (supportsStatisticsSettings(widgets_[slot])) {
      const CarouselSettingsBounds settings = carouselSettingsBounds(cellX, cellY, cellW, cellH);
      renderer.rectangle.fill(settings.x - 4, settings.y - 4, settings.size + 8, settings.size + 8, false);
      renderer.rectangle.render(settings.x - 4, settings.y - 4, settings.size + 8, settings.size + 8, true);
      renderer.bitmap.icon(Setting, settings.x, settings.y, settings.size, settings.size);
    }
    if (supportsStreaksSettings(widgets_[slot])) {
      const CarouselSettingsBounds settings = carouselSettingsBounds(cellX, cellY, cellW, cellH);
      renderer.rectangle.fill(settings.x - 4, settings.y - 4, settings.size + 8, settings.size + 8, false);
      renderer.rectangle.render(settings.x - 4, settings.y - 4, settings.size + 8, settings.size + 8, true);
      renderer.bitmap.icon(Setting, settings.x, settings.y, settings.size, settings.size);
    }
  }
}

void ThemePickerActivity::renderDividerBorder(const HomeTheme::Border border, const int x, const int y,
                                              const int width) {
  if (width <= 0) return;
  switch (border) {
    case HomeTheme::Border::Subtle:
      renderer.line.render(x, y, x + width, y, true, LineRender::Style::Dotted);
      break;
    case HomeTheme::Border::Normal:
      renderer.line.render(x, y, x + width, y, true);
      break;
    case HomeTheme::Border::Thick:
      for (int offset = -1; offset <= 1; ++offset) {
        renderer.line.render(x, y + offset, x + width, y + offset, true);
      }
      break;
    case HomeTheme::Border::None:
    default:
      break;
  }
}

void ThemePickerActivity::renderBorder(const HomeTheme::Border border, const int x, const int y, const int width,
                                       const int height) {
  if (width <= 0 || height <= 0) return;
  if (layout_ == HomeTheme::Layout::OneByOne) {
    if (border == HomeTheme::Border::None) return;
    const int thickness = border == HomeTheme::Border::Thick ? 3 : 1;
    for (int offset = 0; offset < thickness; ++offset) {
      renderer.rectangle.render(x + offset, y + offset, width - offset * 2, height - offset * 2, true,
                                false, false);
    }
    return;
  }
  const int columns = layoutColumns(layout_);
  const int cellW = width / columns;
  const int rows = layoutRows(layout_);
  const int cellH = height / rows;
  switch (border) {
    case HomeTheme::Border::Subtle:
      for (int row = 1; row < rows; ++row) {
        renderer.line.render(x, y + row * cellH, x + width, y + row * cellH, true, LineRender::Style::Dotted);
      }
      if (columns == 2) renderer.line.render(x + cellW, y, x + cellW, y + height, true, LineRender::Style::Dotted);
      break;
    case HomeTheme::Border::Normal:
      for (int row = 1; row < rows; ++row) {
        renderer.line.render(x, y + row * cellH, x + width, y + row * cellH, true);
      }
      if (columns == 2) renderer.line.render(x + cellW, y, x + cellW, y + height, true);
      break;
    case HomeTheme::Border::Thick:
      for (int offset = -1; offset <= 1; ++offset) {
        for (int row = 1; row < rows; ++row) {
          renderer.line.render(x, y + row * cellH + offset, x + width, y + row * cellH + offset, true);
        }
        if (columns == 2) renderer.line.render(x + cellW + offset, y, x + cellW + offset, y + height, true);
      }
      break;
    case HomeTheme::Border::None:
    default:
      break;
  }
}

void ThemePickerActivity::renderWidgetPreview(const HomeTheme::Widget widget, const int x, const int y,
                                              const int width, const int height, const bool background,
                                              const HomeTheme::CarouselStyle style, const bool showLabel,
                                              const HomeTheme::CarouselLabelColor labelColor,
                                              const HomeTheme::CarouselShadowStyle shadowStyle,
                                              const HomeTheme::HeatmapView heatmapView,
                                              const char (*libraryFolders)[128], const bool descriptionShowTitle,
                                              const bool descriptionShowAuthor, const bool descriptionShowProgress,
                                              const bool descriptionShowRating,
                                              const bool recentShowTitle, const bool recentShowAuthor,
                                              const bool recentShowProgress, const bool recentShowRating,
                                              const bool carouselShowProgress, const bool carouselFullHeight,
                                              const bool statisticsSummary, const bool streakCompact,
                                              const bool stackedHeatmapPreview, const bool showEmptyAddLabel) {
  if (width <= 0 || height <= 0) return;

  switch (widget) {
    case HomeTheme::Widget::Carousel:
      carousel_.preview(x, y, width, height, background, style, showLabel, labelColor, shadowStyle,
                        carouselShowProgress, carouselFullHeight, layout_ == HomeTheme::Layout::OneByThree);
      break;
    case HomeTheme::Widget::Recent:
      recent_.preview(x, y, width, height, background, style, showLabel, labelColor, shadowStyle, recentShowTitle,
                      recentShowAuthor, recentShowProgress, recentShowRating, carouselFullHeight);
      break;
    case HomeTheme::Widget::RecentGrid:
      recentGrid_.preview(x, y, width, height);
      break;
    case HomeTheme::Widget::Flow:
      flow_.preview(x, y, width, height, layout_ == HomeTheme::Layout::OneByOne);
      break;
    case HomeTheme::Widget::VerticalBookshelf:
      verticalBookshelf_.preview(renderer, x, y, width, height);
      break;
    case HomeTheme::Widget::Shortcuts:
      shortcut_.render(x, y, width, height);
      break;
    case HomeTheme::Widget::ListShortcuts:
      shortcutList_.render(x, y, width, height);
      break;
    case HomeTheme::Widget::Clock:
      clock_.render(x, y, width, height, layout_ == HomeTheme::Layout::OneByThree);
      break;
    case HomeTheme::Widget::Calendar:
      calendar_.render(x, y, width, height, layout_ == HomeTheme::Layout::OneByThree);
      break;
#if FREEINK_DEVICE_STICKY
    case HomeTheme::Widget::Temperature:
      temperature_.preview(x, y, width, height);
      break;
    case HomeTheme::Widget::Humidity:
      humidity_.preview(x, y, width, height);
      break;
#endif
    case HomeTheme::Widget::TodaysReading:
      todaysReading_.preview(x, y, width, height, layout_ == HomeTheme::Layout::OneByThree);
      break;
    case HomeTheme::Widget::Statistics:
      weeklyStatistics_.preview(x, y, width, height, layout_ == HomeTheme::Layout::OneByThree,
                                style == HomeTheme::CarouselStyle::Left, statisticsSummary);
      break;
    case HomeTheme::Widget::Streaks:
      streaks_.preview(x, y, width, height, layout_ == HomeTheme::Layout::OneByTwo && !streakCompact);
      break;
    case HomeTheme::Widget::Favorites:
      favorites_.preview(x, y, width, height, background, style, showLabel, labelColor, shadowStyle,
                         carouselFullHeight, layout_ == HomeTheme::Layout::OneByThree);
      break;
    case HomeTheme::Widget::Heatmap:
      heatmap_.preview(x, y, width, height, HomeTheme::HeatmapView::Monthly, false, labelColor,
                       stackedHeatmapPreview || layout_ == HomeTheme::Layout::OneByTwo);
      break;
    case HomeTheme::Widget::Library:
      drawLibraryPreviewPlaceholders(renderer, x, y, width, height, background, showLabel,
                                     layout_ == HomeTheme::Layout::OneByThree);
      break;
    case HomeTheme::Widget::Description:
      description_.render(0, x, y, width, height, background, showLabel, labelColor, shadowStyle,
                          descriptionShowTitle, descriptionShowAuthor, descriptionShowProgress,
                          descriptionShowRating, layout_ == HomeTheme::Layout::OneByThree, true);
      break;
    case HomeTheme::Widget::Empty:
    default:
      renderer.rectangle.fill(x, y, width, height, false);
      {
        const int labelFont = systemFontId();
        const char* label = showEmptyAddLabel ? "Add widget +" : "Empty";
        const int labelWidth = renderer.text.getWidth(labelFont, label);
        const int labelY = y + (height - renderer.text.getLineHeight(labelFont)) / 2;
        renderer.text.render(labelFont, x + std::max(0, (width - labelWidth) / 2), labelY, label, true,
                             EpdFontFamily::REGULAR);
      }
      break;
  }
}

void ThemePickerActivity::renderWidgetPopup() {
  const int options = widgetOptionCount(layout_, editingSleep_);
  SubPage::header(renderer, "Choose widget");
  const WidgetGridBounds box = widgetGridBounds(renderer);
  const int font = systemFontId();

  const int previewLabelHeight = renderer.text.getLineHeight(font);
  const int pageEnd = std::min(options, widgetPopupScroll_ + kWidgetGridPageSize);
  for (int index = widgetPopupScroll_; index < pageEnd; ++index) {
    const int pageIndex = index - widgetPopupScroll_;
    const int row = pageIndex / kWidgetGridColumns;
    const int column = pageIndex % kWidgetGridColumns;
    const int cellX = box.gridX + column * (box.cellWidth + 12);
    const int cellY = box.gridY + row * (box.cellHeight + 12);
    renderer.rectangle.fill(cellX, cellY, box.cellWidth, box.cellHeight, false);

    const int previewX = cellX + 10;
    const int previewY = cellY + 8;
    const int previewW = std::max(8, box.cellWidth - 20);
    const int previewH = std::max(8, box.cellHeight - previewLabelHeight - 18);
    const HomeTheme::Widget widget = widgetOptionAt(layout_, editingSleep_, index);
    renderWidgetPreview(widget, previewX, previewY, previewW, previewH, false,
                        HomeTheme::defaultCarouselStyle(widget), false,
                        HomeTheme::CarouselLabelColor::Black, HomeTheme::CarouselShadowStyle::None,
                        HomeTheme::HeatmapView::Monthly, libraryFolders_[widgetSlot_], true, true, true, true,
                        true, true, true, true, true, false, true, false, true);

    // Empty already has its label inside the placeholder preview. Do not
    // repeat it beneath the cell like the other widget options.
    if (widget != HomeTheme::Widget::Empty) {
      const std::string label = HomeTheme::widgetLabel(widget);
      const std::string shown = renderer.text.truncate(font, label.c_str(), box.cellWidth - 16);
      const int labelY = cellY + box.cellHeight - previewLabelHeight - 7;
      const int labelWidth = renderer.text.getWidth(font, shown.c_str());
      renderer.text.render(font, cellX + (box.cellWidth - labelWidth) / 2, labelY, shown.c_str(), true,
                           EpdFontFamily::REGULAR);
    }
    if (index + kWidgetGridColumns < pageEnd) {
      renderer.line.render(cellX, cellY + box.cellHeight + 6, cellX + box.cellWidth, cellY + box.cellHeight + 6,
                           true, LineRender::Style::Dotted);
    }
  }

  const int totalRows = (options + kWidgetGridColumns - 1) / kWidgetGridColumns;
  const int visibleRows = kWidgetGridRows;
  const int pageCount = std::max(1, (totalRows + visibleRows - 1) / visibleRows);
  const int activePage = std::min(pageCount - 1, widgetPopupScroll_ / kWidgetGridPageSize);
  constexpr int indicatorRightPadding = 20;
  const int indicatorX = box.x + box.width - indicatorRightPadding;
  Paginator::render(renderer, activePage, pageCount, indicatorX, box.gridY, box.cellHeight * visibleRows);
}

void ThemePickerActivity::renderBorderPopup() {
  static const char* const labels[] = {"None", "Subtle", "Normal", "Thick"};
  std::vector<std::string> values;
  for (const char* label : labels) values.emplace_back(label);
  const PopUpBounds box = PopUp::bounds(renderer, static_cast<int>(values.size()));
  PopUp::background(renderer, box);
  PopUp::title(renderer, box, "Border");
  PopUp::list(renderer, box, values, borderPopupSelected_, 0);
  PopUp::border(renderer, box);
}

void ThemePickerActivity::editTheme() {
  editingSleep_ = sleepMode_;
  const HomeTheme::Theme& theme = editingSleep_ ? HomeTheme::sleepAt(selected_) : HomeTheme::at(selected_);
  layout_ = theme.layout;
  for (int i = 0; i < 4; ++i) {
    widgets_[i] = theme.widgets[i];
    borders_[i] = theme.borders[i];
    backgrounds_[i] = theme.backgrounds[i];
    carouselStyles_[i] = theme.carouselStyles[i];
    carouselLabels_[i] = theme.carouselLabels[i];
    descriptionTitles_[i] = theme.descriptionTitles[i];
    descriptionAuthors_[i] = theme.descriptionAuthors[i];
    descriptionProgress_[i] = theme.descriptionProgress[i];
    descriptionRatings_[i] = theme.descriptionRatings[i];
    recentTitles_[i] = theme.recentTitles[i];
    recentAuthors_[i] = theme.recentAuthors[i];
    recentProgress_[i] = theme.recentProgress[i];
    recentRatings_[i] = theme.recentRatings[i];
    carouselProgress_[i] = theme.carouselProgress[i];
    carouselFullHeight_[i] = theme.carouselFullHeight[i];
    statisticsSummary_[i] = theme.statisticsSummary[i];
    streakCompact_[i] = theme.streakCompact[i];
    carouselLabelColors_[i] = theme.carouselLabelColors[i];
    carouselShadowStyles_[i] = theme.carouselShadowStyles[i];
    heatmapViews_[i] = theme.heatmapViews[i];
    for (int folder = 0; folder < LibraryWidget::kFolderCount; ++folder) {
      std::strncpy(libraryFolders_[i][folder], theme.libraryFolders[i][folder],
                   sizeof(libraryFolders_[i][folder]) - 1);
      libraryFolders_[i][folder][sizeof(libraryFolders_[i][folder]) - 1] = '\0';
      if (libraryFolders_[i][folder][0] != '/') setDefaultLibraryFolders(libraryFolders_[i]);
    }
#if !FREEINK_DEVICE_STICKY
    if (widgets_[i] == HomeTheme::Widget::Temperature || widgets_[i] == HomeTheme::Widget::Humidity) {
      widgets_[i] = HomeTheme::Widget::Empty;
      borders_[i] = HomeTheme::Border::None;
      carouselStyles_[i] = HomeTheme::CarouselStyle::Centered;
      carouselLabels_[i] = 0;
      carouselLabelColors_[i] = HomeTheme::CarouselLabelColor::Black;
      carouselShadowStyles_[i] = HomeTheme::CarouselShadowStyle::None;
      heatmapViews_[i] = HomeTheme::HeatmapView::Weekly;
    }
#endif
  }
  if (layout_ == HomeTheme::Layout::OneByOne) {
    bool hasFlow = false;
    for (const HomeTheme::Widget widget : widgets_) hasFlow = hasFlow || widget == HomeTheme::Widget::Flow;
    if (!hasFlow && widgets_[1] == HomeTheme::Widget::Empty) widgets_[1] = HomeTheme::Widget::Flow;
  }
  if (layout_ == HomeTheme::Layout::Classic) {
    for (HomeTheme::Widget& widget : widgets_) widget = HomeTheme::Widget::Empty;
    for (HomeTheme::Border& border : borders_) border = HomeTheme::Border::None;
    for (HomeTheme::CarouselStyle& style : carouselStyles_) style = HomeTheme::CarouselStyle::Centered;
    for (uint8_t& label : carouselLabels_) label = 0;
    for (uint8_t& value : descriptionTitles_) value = 1;
    for (uint8_t& value : descriptionAuthors_) value = 1;
    for (uint8_t& value : descriptionProgress_) value = 1;
    for (uint8_t& value : descriptionRatings_) value = 1;
    for (uint8_t& value : recentTitles_) value = 1;
    for (uint8_t& value : recentAuthors_) value = 1;
    for (uint8_t& value : recentProgress_) value = 1;
    for (uint8_t& value : recentRatings_) value = 1;
    for (uint8_t& value : carouselProgress_) value = 1;
    for (uint8_t& value : carouselFullHeight_) value = 0;
    for (uint8_t& value : statisticsSummary_) value = 1;
    for (uint8_t& value : streakCompact_) value = 0;
    for (HomeTheme::CarouselLabelColor& color : carouselLabelColors_) color = HomeTheme::CarouselLabelColor::Black;
    for (HomeTheme::CarouselShadowStyle& style : carouselShadowStyles_) {
      style = HomeTheme::CarouselShadowStyle::None;
    }
    for (HomeTheme::HeatmapView& view : heatmapViews_) view = HomeTheme::HeatmapView::Weekly;
  }
  widgetSlot_ = 0;
  widgetPageIndex_ = 0;
  editingExisting_ = true;
  creatingTheme_ = false;
  layoutSelection_ = layoutIndex(layout_);
  screen_ = Screen::Layouts;
  render();
}

void ThemePickerActivity::beginEditTheme() { editTheme(); }

void ThemePickerActivity::initializeNewThemeEditor() {
  for (int i = 0; i < 4; ++i) {
    widgets_[i] = HomeTheme::Widget::Empty;
    borders_[i] = HomeTheme::Border::None;
    backgrounds_[i] = 0;
    carouselStyles_[i] = HomeTheme::CarouselStyle::Centered;
    carouselLabels_[i] = 0;
    carouselLabelColors_[i] = HomeTheme::CarouselLabelColor::Black;
    carouselShadowStyles_[i] = HomeTheme::CarouselShadowStyle::None;
    descriptionTitles_[i] = 1;
    descriptionAuthors_[i] = 1;
    descriptionProgress_[i] = 1;
    descriptionRatings_[i] = 1;
    recentTitles_[i] = 1;
    recentAuthors_[i] = 1;
    recentProgress_[i] = 1;
    recentRatings_[i] = 1;
    carouselProgress_[i] = 1;
    carouselFullHeight_[i] = 0;
    statisticsSummary_[i] = 1;
    streakCompact_[i] = 0;
    heatmapViews_[i] = HomeTheme::HeatmapView::Weekly;
    setDefaultLibraryFolders(libraryFolders_[i]);
  }
  if (editingSleep_) {
    widgets_[0] = HomeTheme::Widget::Clock;
    if (HomeTheme::slotCount(layout_) > 1) widgets_[1] = HomeTheme::Widget::Streaks;
  } else if (layout_ == HomeTheme::Layout::OneByOne) {
    widgets_[0] = HomeTheme::Widget::RecentGrid;
    widgets_[1] = HomeTheme::Widget::Flow;
  } else {
    widgets_[0] = HomeTheme::Widget::Carousel;
    widgets_[1] = HomeTheme::Widget::Description;
    carouselStyles_[0] = HomeTheme::CarouselStyle::Left;
    backgrounds_[0] = 1;
    carouselShadowStyles_[0] = HomeTheme::CarouselShadowStyle::Gray;
    carouselProgress_[0] = 1;
    if (layout_ == HomeTheme::Layout::OneByThree) widgets_[2] = HomeTheme::Widget::Empty;
  }
}

void ThemePickerActivity::applySelectedLayout() {
  const HomeTheme::Layout nextLayout = layoutAtIndex(layoutSelection_);
  if (creatingTheme_) {
    layout_ = nextLayout;
    initializeNewThemeEditor();
  } else {
    const HomeTheme::Layout previousLayout = layout_;
    layout_ = nextLayout;
    if (editingSleep_) {
      for (int slot = 0; slot < 4; ++slot) {
        widgets_[slot] = HomeTheme::Widget::Empty;
        borders_[slot] = HomeTheme::Border::None;
      }
      widgets_[0] = HomeTheme::Widget::Clock;
      if (HomeTheme::slotCount(layout_) > 1) widgets_[1] = HomeTheme::Widget::Streaks;
    } else if (layout_ == HomeTheme::Layout::OneByOne) {
      widgets_[0] = HomeTheme::Widget::RecentGrid;
      widgets_[1] = HomeTheme::Widget::Flow;
      borders_[0] = HomeTheme::Border::None;
      borders_[1] = HomeTheme::Border::None;
      for (int slot = 2; slot < 4; ++slot) {
        widgets_[slot] = HomeTheme::Widget::Empty;
        borders_[slot] = HomeTheme::Border::None;
      }
    } else if (previousLayout == HomeTheme::Layout::OneByOne) {
      widgets_[0] = HomeTheme::Widget::Carousel;
      widgets_[1] = HomeTheme::Widget::Description;
      widgets_[2] = HomeTheme::Widget::Empty;
      widgets_[3] = HomeTheme::Widget::Empty;
      borders_[2] = HomeTheme::Border::None;
      borders_[3] = HomeTheme::Border::None;
    }
    const int slots = HomeTheme::slotCount(layout_);
    for (int slot = slots; slot < 4; ++slot) {
      widgets_[slot] = HomeTheme::Widget::Empty;
      borders_[slot] = HomeTheme::Border::None;
    }
  }
  widgetSlot_ = 0;
  widgetPageIndex_ = 0;
  widgetPopup_ = false;
  borderPopup_ = false;
  screen_ = Screen::Widgets;
  render();
}

void ThemePickerActivity::beginNewTheme() {
  creatingTheme_ = true;
  editingExisting_ = false;
  editingSleep_ = sleepMode_;
  selected_ = sleepMode_ ? HomeTheme::sleepCount() : HomeTheme::count();
  layout_ = HomeTheme::Layout::OneByTwo;
  layoutSelection_ = layoutIndex(layout_);
  screen_ = Screen::Layouts;
  render();
}

void ThemePickerActivity::returnToThemeList() {
  saveEditorState();
  screen_ = Screen::List;
  widgetPopup_ = false;
  borderPopup_ = false;
  themeActionPopup_ = false;
  const int themeCount = sleepMode_ ? HomeTheme::sleepCount() : HomeTheme::count();
  listCursor_ = creatingTheme_ ? themeCount : selected_ + 1;
  creatingTheme_ = false;
  render();
}

void ThemePickerActivity::renameSelectedTheme() {
  const bool sleepTheme = sleepMode_;
  const std::string current = sleepTheme ? HomeTheme::sleepAt(selected_).name : HomeTheme::at(selected_).name;
  enterNewActivity(new KeyboardEntryActivity(
      renderer, mappedInput, "Rename theme", current, 10, 31, false,
      [this, sleepTheme](const std::string& value) {
        exitActivity();
        if (!value.empty()) {
          if (sleepTheme) {
            HomeTheme::renameSleep(selected_, value.c_str());
          } else {
            HomeTheme::rename(selected_, value.c_str());
          }
        }
        themeActionPopup_ = false;
        render();
      },
      [this] {
        exitActivity();
        render();
      }));
}

void ThemePickerActivity::handleThemeAction(const int action) {
  themeActionSelected_ = action;
  if (action == 0) {
    themeActionPopup_ = false;
    renameSelectedTheme();
    return;
  }
  if (action == 1) {
    themeActionPopup_ = false;
    beginEditTheme();
    return;
  }
  if (action == 2) {
    themeActionPopup_ = false;
    const bool removed = sleepMode_ ? HomeTheme::removeSleep(selected_) : HomeTheme::remove(selected_);
    if (removed) {
      const int count = sleepMode_ ? HomeTheme::sleepCount() : HomeTheme::count();
      selected_ = std::min(selected_, count - 1);
      listCursor_ = selected_ + 1;
    }
    render();
  }
}

void ThemePickerActivity::moveThemeActionSelection(const int delta) {
  themeActionSelected_ = (themeActionSelected_ + 3 + delta) % 3;
}

void ThemePickerActivity::promoteSelectedOneByOneWidget() {
  if (layout_ != HomeTheme::Layout::OneByOne || widgetPageIndex_ <= 0) return;
  const int selectedSlot = oneByOneSlotAtPage(widgets_, widgetPageIndex_);
  if (selectedSlot <= 0 || selectedSlot >= 4) return;
  std::swap(widgets_[0], widgets_[selectedSlot]);
  std::swap(borders_[0], borders_[selectedSlot]);
  std::swap(backgrounds_[0], backgrounds_[selectedSlot]);
  std::swap(carouselStyles_[0], carouselStyles_[selectedSlot]);
  std::swap(carouselLabels_[0], carouselLabels_[selectedSlot]);
  std::swap(descriptionTitles_[0], descriptionTitles_[selectedSlot]);
  std::swap(descriptionAuthors_[0], descriptionAuthors_[selectedSlot]);
  std::swap(descriptionProgress_[0], descriptionProgress_[selectedSlot]);
  std::swap(descriptionRatings_[0], descriptionRatings_[selectedSlot]);
  std::swap(recentTitles_[0], recentTitles_[selectedSlot]);
  std::swap(recentAuthors_[0], recentAuthors_[selectedSlot]);
  std::swap(recentProgress_[0], recentProgress_[selectedSlot]);
  std::swap(recentRatings_[0], recentRatings_[selectedSlot]);
  std::swap(carouselProgress_[0], carouselProgress_[selectedSlot]);
  std::swap(carouselFullHeight_[0], carouselFullHeight_[selectedSlot]);
  std::swap(statisticsSummary_[0], statisticsSummary_[selectedSlot]);
  std::swap(streakCompact_[0], streakCompact_[selectedSlot]);
  std::swap(carouselLabelColors_[0], carouselLabelColors_[selectedSlot]);
  std::swap(carouselShadowStyles_[0], carouselShadowStyles_[selectedSlot]);
  std::swap(heatmapViews_[0], heatmapViews_[selectedSlot]);
  for (int folder = 0; folder < LibraryWidget::kFolderCount; ++folder) {
    std::swap(libraryFolders_[0][folder], libraryFolders_[selectedSlot][folder]);
  }
  widgetSlot_ = 0;
  widgetPageIndex_ = 0;
}

void ThemePickerActivity::saveEditorState() {
  if (screen_ == Screen::Widgets && editingExisting_ && !widgetPopup_ && !borderPopup_) {
    promoteSelectedOneByOneWidget();
    if (editingSleep_) {
      HomeTheme::updateSleepAt(selected_, layout_, widgets_, borders_, backgrounds_, carouselStyles_, carouselLabels_,
                             carouselLabelColors_, carouselShadowStyles_, heatmapViews_, descriptionTitles_,
                             descriptionAuthors_, descriptionProgress_, descriptionRatings_,
                             recentTitles_, recentAuthors_, recentProgress_,
                             recentRatings_, carouselProgress_, carouselFullHeight_, statisticsSummary_, libraryFolders_,
                             HomeTheme::slotCount(layout_));
      HomeTheme::updateStreakCompact(selected_, true, streakCompact_);
    } else {
        HomeTheme::update(selected_, layout_, widgets_, borders_, backgrounds_, carouselStyles_, carouselLabels_,
                        carouselLabelColors_, carouselShadowStyles_, heatmapViews_, descriptionTitles_,
                        descriptionAuthors_, descriptionProgress_, descriptionRatings_,
                        recentTitles_, recentAuthors_, recentProgress_,
                        recentRatings_, carouselProgress_, carouselFullHeight_, statisticsSummary_, libraryFolders_,
                        HomeTheme::slotCount(layout_));
      HomeTheme::updateStreakCompact(selected_, false, streakCompact_);
    }
  }
}

void ThemePickerActivity::saveEditorAndClose() {
  if (screen_ != Screen::Widgets || widgetPopup_ || borderPopup_) return;
  promoteSelectedOneByOneWidget();
  if (creatingTheme_) {
    const int index = editingSleep_
                          ? HomeTheme::addSleep(layout_, widgets_, borders_, backgrounds_, carouselStyles_, carouselLabels_,
                                                carouselLabelColors_, carouselShadowStyles_, heatmapViews_,
                                                descriptionTitles_, descriptionAuthors_, descriptionProgress_,
                                                descriptionRatings_, recentTitles_, recentAuthors_, recentProgress_,
                                                recentRatings_, carouselProgress_, carouselFullHeight_,
                                                statisticsSummary_, libraryFolders_, HomeTheme::slotCount(layout_))
                          : HomeTheme::add(layout_, widgets_, borders_, backgrounds_, carouselStyles_, carouselLabels_,
                                     carouselLabelColors_, carouselShadowStyles_, heatmapViews_, descriptionTitles_,
                                     descriptionAuthors_, descriptionProgress_, descriptionRatings_, recentTitles_,
                                     recentAuthors_, recentProgress_, recentRatings_, carouselProgress_,
                                     carouselFullHeight_, statisticsSummary_, libraryFolders_,
                                     HomeTheme::slotCount(layout_));
    if (index >= 0) {
      selected_ = index;
      HomeTheme::updateStreakCompact(selected_, editingSleep_, streakCompact_);
    }
  } else if (editingSleep_) {
    HomeTheme::updateSleepAt(selected_, layout_, widgets_, borders_, backgrounds_, carouselStyles_, carouselLabels_,
                           carouselLabelColors_, carouselShadowStyles_, heatmapViews_, descriptionTitles_,
                           descriptionAuthors_, descriptionProgress_, descriptionRatings_, recentTitles_,
                           recentAuthors_, recentProgress_, recentRatings_, carouselProgress_, carouselFullHeight_,
                           statisticsSummary_, libraryFolders_, HomeTheme::slotCount(layout_));
    HomeTheme::updateStreakCompact(selected_, true, streakCompact_);
  } else {
    HomeTheme::update(selected_, layout_, widgets_, borders_, backgrounds_, carouselStyles_, carouselLabels_,
                     carouselLabelColors_, carouselShadowStyles_, heatmapViews_, descriptionTitles_,
                     descriptionAuthors_, descriptionProgress_, descriptionRatings_, recentTitles_, recentAuthors_,
                     recentProgress_, recentRatings_, carouselProgress_, carouselFullHeight_, statisticsSummary_,
                     libraryFolders_, HomeTheme::slotCount(layout_));
    HomeTheme::updateStreakCompact(selected_, false, streakCompact_);
  }
  returnToThemeList();
}

void ThemePickerActivity::openWidgetPopup(const int slot) {
  widgetSlot_ = slot;
  popupSelected_ = widgetOptionIndex(layout_, editingSleep_, widgets_[slot]);
  const int options = widgetOptionCount(layout_, editingSleep_);
  const int pageStart = (popupSelected_ / kWidgetGridPageSize) * kWidgetGridPageSize;
  widgetPopupScroll_ = std::max(0, std::min(pageStart, std::max(0, options - 1)));
  widgetPopup_ = true;
  render();
}

void ThemePickerActivity::openBorderPopup(const int slot) {
  widgetSlot_ = slot;
  borderPopupSelected_ = static_cast<int>(borders_[slot]);
  borderPopup_ = true;
  render();
}

void ThemePickerActivity::openCarouselSettings(const int slot) {
  widgetSlot_ = slot;
  carouselSettingsFinished_ = false;
  enterNewActivity(new BaseCarouselActivity(
      renderer, mappedInput, carouselStyles_[slot], backgrounds_[slot], carouselLabels_[slot] != 0,
      carouselLabelColors_[slot], carouselShadowStyles_[slot],
      [this, slot](const HomeTheme::CarouselStyle style, const bool background, const bool showLabel,
                   const HomeTheme::CarouselLabelColor labelColor,
                   const HomeTheme::CarouselShadowStyle shadowStyle, const bool showTitle, const bool showAuthor,
                   const bool showProgress, const bool showRating, const bool fullHeight) {
        carouselStyles_[slot] = style;
        backgrounds_[slot] = background ? 1 : 0;
        carouselLabels_[slot] = showLabel ? 1 : 0;
        carouselLabelColors_[slot] = labelColor;
        carouselShadowStyles_[slot] = shadowStyle;
        carouselFullHeight_[slot] = fullHeight ? 1 : 0;
        if (widgets_[slot] == HomeTheme::Widget::Recent) {
          recentTitles_[slot] = showTitle ? 1 : 0;
          recentAuthors_[slot] = showAuthor ? 1 : 0;
          recentProgress_[slot] = showProgress ? 1 : 0;
          recentRatings_[slot] = showRating ? 1 : 0;
        } else if (widgets_[slot] == HomeTheme::Widget::Carousel) {
          carouselProgress_[slot] = showProgress ? 1 : 0;
        }
        carouselSettingsFinished_ = true;
      },
      [] {}, widgets_[slot] == HomeTheme::Widget::Recent,
      widgets_[slot] == HomeTheme::Widget::Recent ? recentTitles_[slot] != 0 : true,
      widgets_[slot] == HomeTheme::Widget::Recent ? recentAuthors_[slot] != 0 : true,
      widgets_[slot] == HomeTheme::Widget::Recent ? recentProgress_[slot] != 0 : carouselProgress_[slot] != 0,
      widgets_[slot] == HomeTheme::Widget::Recent ? recentRatings_[slot] != 0 : true,
      carouselFullHeight_[slot] != 0));
}

void ThemePickerActivity::openHeatmapSettings(const int slot) {
  widgetSlot_ = slot;
  heatmapSettingsFinished_ = false;
  enterNewActivity(new BaseHeatmapActivity(
      renderer, mappedInput, heatmapViews_[slot], carouselLabels_[slot] != 0, carouselLabelColors_[slot],
      [this, slot](const HomeTheme::HeatmapView view, const bool showLabel,
                   const HomeTheme::CarouselLabelColor labelColor) {
        heatmapViews_[slot] = view;
        carouselLabels_[slot] = showLabel ? 1 : 0;
        carouselLabelColors_[slot] = labelColor;
        heatmapSettingsFinished_ = true;
      },
      [] {}));
}

void ThemePickerActivity::openStatisticsSettings(const int slot) {
  widgetSlot_ = slot;
  statisticsSettingsFinished_ = false;
  enterNewActivity(new BaseStatisticsActivity(
      renderer, mappedInput, carouselStyles_[slot] == HomeTheme::CarouselStyle::Left,
      statisticsSummary_[slot] != 0,
      [this, slot](const bool lineChart, const bool showSummary) {
        carouselStyles_[slot] = lineChart ? HomeTheme::CarouselStyle::Left : HomeTheme::CarouselStyle::Centered;
        statisticsSummary_[slot] = showSummary ? 1 : 0;
        statisticsSettingsFinished_ = true;
      },
      [] {}));
}

void ThemePickerActivity::openStreaksSettings(const int slot) {
  widgetSlot_ = slot;
  streaksSettingsFinished_ = false;
  enterNewActivity(new BaseStreaksActivity(
      renderer, mappedInput, layout_ == HomeTheme::Layout::OneByThree || streakCompact_[slot] != 0,
      [this, slot](const bool compact) {
        streakCompact_[slot] = compact ? 1 : 0;
        streaksSettingsFinished_ = true;
      },
      [] {}));
}

void ThemePickerActivity::openTemperatureSettings(const int slot) {
  (void)slot;
  temperatureSettingsFinished_ = false;
  bool fahrenheit = false;
  temperature_preferences::loadFahrenheit(fahrenheit);
  enterNewActivity(new BaseTemperatureActivity(
      renderer, mappedInput, fahrenheit,
      [this](const bool selectedFahrenheit) {
        temperature_preferences::saveFahrenheit(selectedFahrenheit);
        temperature_.reloadPreferences();
        temperatureSettingsFinished_ = true;
      },
      [] {}));
}

void ThemePickerActivity::openLibrarySettings(const int slot) {
  widgetSlot_ = slot;
  librarySettingsFinished_ = false;
  LibraryWidget::FolderPaths folders;
  for (int index = 0; index < LibraryWidget::kFolderCount; ++index) {
    folders[static_cast<size_t>(index)] = libraryFolders_[slot][index][0] == '/'
                                              ? libraryFolders_[slot][index]
                                              : "/";
  }
  enterNewActivity(new BaseLibraryActivity(
      renderer, mappedInput, folders, backgrounds_[slot] != 0, carouselLabels_[slot] != 0,
      [this, slot](const LibraryWidget::FolderPaths& selectedFolders, const bool background, const bool showLabel) {
        for (int index = 0; index < LibraryWidget::kFolderCount; ++index) {
          std::strncpy(libraryFolders_[slot][index], selectedFolders[static_cast<size_t>(index)].c_str(),
                       sizeof(libraryFolders_[slot][index]) - 1);
          libraryFolders_[slot][index][sizeof(libraryFolders_[slot][index]) - 1] = '\0';
        }
        backgrounds_[slot] = background ? 1 : 0;
        carouselLabels_[slot] = showLabel ? 1 : 0;
        librarySettingsFinished_ = true;
      },
      [] {}));
}

void ThemePickerActivity::openDescriptionSettings(const int slot) {
  widgetSlot_ = slot;
  descriptionSettingsFinished_ = false;
  enterNewActivity(new BaseDescriptionActivity(
      renderer, mappedInput, backgrounds_[slot] != 0, descriptionTitles_[slot] != 0,
      descriptionAuthors_[slot] != 0, descriptionProgress_[slot] != 0,
      descriptionRatings_[slot] != 0,
      [this, slot](const bool background, const bool showTitle, const bool showAuthor, const bool showProgress,
                   const bool showRating) {
        backgrounds_[slot] = background ? 1 : 0;
        descriptionTitles_[slot] = showTitle ? 1 : 0;
        descriptionAuthors_[slot] = showAuthor ? 1 : 0;
        descriptionProgress_[slot] = showProgress ? 1 : 0;
        descriptionRatings_[slot] = showRating ? 1 : 0;
        descriptionSettingsFinished_ = true;
      },
      [] {}));
}

void ThemePickerActivity::moveWidgetPopupSelection(const int delta) {
  const int options = widgetOptionCount(layout_, editingSleep_);
  if (options <= 0) return;
  popupSelected_ = (popupSelected_ + options + delta) % options;
  widgetPopupScroll_ = (popupSelected_ / kWidgetGridPageSize) * kWidgetGridPageSize;
}

void ThemePickerActivity::pageWidgetPopup(const int delta) {
  const int options = widgetOptionCount(layout_, editingSleep_);
  if (options <= 0) return;
  const int maxPageStart = ((options - 1) / kWidgetGridPageSize) * kWidgetGridPageSize;
  const int nextPage = widgetPopupScroll_ + delta * kWidgetGridPageSize;
  if (nextPage > maxPageStart) {
    widgetPopupScroll_ = 0;
    popupSelected_ = 0;
  } else if (nextPage < 0) {
    widgetPopupScroll_ = maxPageStart;
    popupSelected_ = options - 1;
  } else {
    widgetPopupScroll_ = nextPage;
    popupSelected_ = std::max(widgetPopupScroll_,
                              std::min(popupSelected_,
                                       std::min(options - 1, widgetPopupScroll_ + kWidgetGridPageSize - 1)));
  }
}

void ThemePickerActivity::cycleOneByOneWidget(const int delta) {
  if (layout_ != HomeTheme::Layout::OneByOne || editingSleep_) return;
  const int slot = oneByOneSlotAtPage(widgets_, widgetPageIndex_);
  const int options = widgetOptionCount(HomeTheme::Layout::OneByOne, false);
  if (options <= 0) return;

  int current = widgetOptionIndex(HomeTheme::Layout::OneByOne, false, widgets_[slot]);
  current = (current + delta + options) % options;
  widgets_[slot] = widgetOptionAt(HomeTheme::Layout::OneByOne, false, current);
  borders_[slot] = HomeTheme::Border::None;
  render();
}

void ThemePickerActivity::switchLayout(const HomeTheme::Layout layout) {
  if (layout == layout_) return;
  if (layout == HomeTheme::Layout::OneByOne) {
    layout_ = layout;
    widgets_[0] = HomeTheme::Widget::RecentGrid;
    borders_[0] = HomeTheme::Border::None;
    widgets_[1] = HomeTheme::Widget::Flow;
    for (int slot = 2; slot < 4; ++slot) {
      widgets_[slot] = HomeTheme::Widget::Empty;
      borders_[slot] = HomeTheme::Border::None;
    }
  } else if (layout_ == HomeTheme::Layout::OneByOne) {
    layout_ = layout;
    widgets_[0] = HomeTheme::Widget::Carousel;
    widgets_[1] = HomeTheme::Widget::Description;
    widgets_[2] = HomeTheme::Widget::Empty;
    widgets_[3] = HomeTheme::Widget::Empty;
  } else {
    layout_ = layout;
  }
  widgetSlot_ = 0;
  widgetPageIndex_ = 0;
  widgetPopup_ = false;
  borderPopup_ = false;
  saveEditorState();
  render();
}

void ThemePickerActivity::handleTouch(const int x, const int y) {
  if (screen_ == Screen::Mode) {
    const int top = kThemeContentTop;
    if (y >= top && y < top + 2 * kRowHeight) {
      sleepMode_ = y >= top + kRowHeight;
      selected_ = sleepMode_ ? HomeTheme::sleepActiveIndex() : HomeTheme::activeIndex();
      listCursor_ = selected_ + 1;
      screen_ = Screen::List;
      render();
    }
    return;
  }
  if (screen_ == Screen::List) {
    const int contentTop = kThemeContentTop;
    if (themeActionPopup_) {
      const PopUpBounds box = PopUp::bounds(renderer, 3);
      if (!inside(x, y, box.x, box.y, box.width, box.height)) {
        themeActionPopup_ = false;
        render();
        return;
      }
      const int optionY = y - box.y - box.header;
      if (optionY < 0 || optionY >= box.rows * box.row) return;
      handleThemeAction(std::min(2, optionY / box.row));
      return;
    }
    const ButtonBounds newThemeButton = newThemeButtonBounds(renderer, systemFontId());
    if (x >= newThemeButton.x && x < newThemeButton.x + newThemeButton.width && y >= newThemeButton.y &&
        y < newThemeButton.y + newThemeButton.height) {
      beginNewTheme();
      return;
    }
    const int themeListTop = contentTop;
    const int themeCount = sleepMode_ ? HomeTheme::sleepCount() : HomeTheme::count();
    if (y >= themeListTop && y < themeListTop + themeCount * kRowHeight) {
      const int index = (y - themeListTop) / kRowHeight;
      if (index < 0 || index >= themeCount) return;
      selected_ = index;
      listCursor_ = index + 1;
      const int toggleLeft = renderer.getScreenWidth() - 84;
      if (x >= toggleLeft) {
        if (sleepMode_) {
          HomeTheme::activateSleep(index);
        } else {
          HomeTheme::activate(index);
        }
        render();
      } else {
        themeActionSelected_ = 0;
        themeActionPopup_ = true;
        render();
      }
      return;
    }
    return;
  }
  if (screen_ == Screen::Layouts) {
    const int contentTop = kThemeContentTop;
    const int availableHeight = std::max(1, renderer.getScreenHeight() - contentTop);
    const int sectionHeight = std::max(1, availableHeight / 3);
    if (y >= contentTop && y < contentTop + availableHeight) {
      layoutSelection_ = std::min(2, std::max(0, (y - contentTop) / sectionHeight));
      applySelectedLayout();
    }
    return;
  } else {
    if (borderPopup_) {
      const PopUpBounds box = PopUp::bounds(renderer, 4);
      if (!inside(x, y, box.x, box.y, box.width, box.height)) {
        borderPopup_ = false;
        render();
        return;
      }
      const int optionY = y - box.y - box.header;
      if (optionY < 0 || optionY >= box.rows * box.row) return;
      const int selected = optionY / box.row;
      if (selected >= 4) return;
      borders_[widgetSlot_] = static_cast<HomeTheme::Border>(selected);
      borderPopup_ = false;
      render();
      return;
    }
    if (widgetPopup_) {
      const int options = widgetOptionCount(layout_, editingSleep_);
      const WidgetGridBounds box = widgetGridBounds(renderer);
      if (!inside(x, y, box.x, box.y, box.width, box.height)) {
        widgetPopup_ = false;
        render();
        return;
      }
      const int column = (x - box.gridX) / (box.cellWidth + 12);
      const int row = (y - box.gridY) / (box.cellHeight + 12);
      if (column < 0 || column >= kWidgetGridColumns || row < 0 || row >= kWidgetGridRows) return;
      const int cellX = box.gridX + column * (box.cellWidth + 12);
      const int cellY = box.gridY + row * (box.cellHeight + 12);
      if (!inside(x, y, cellX, cellY, box.cellWidth, box.cellHeight)) return;
      popupSelected_ = widgetPopupScroll_ + row * kWidgetGridColumns + column;
      if (popupSelected_ >= options) return;
      const bool wasLibrary = widgets_[widgetSlot_] == HomeTheme::Widget::Library;
      const bool wasDescription = widgets_[widgetSlot_] == HomeTheme::Widget::Description;
      const bool wasRecent = widgets_[widgetSlot_] == HomeTheme::Widget::Recent;
      const bool wasCarousel = widgets_[widgetSlot_] == HomeTheme::Widget::Carousel;
      const bool wasStreaks = widgets_[widgetSlot_] == HomeTheme::Widget::Streaks;
      widgets_[widgetSlot_] = widgetOptionAt(layout_, editingSleep_, popupSelected_);
      if (widgets_[widgetSlot_] == HomeTheme::Widget::Empty) borders_[widgetSlot_] = HomeTheme::Border::None;
      if (layout_ == HomeTheme::Layout::OneByOne) {
        const int itemCount = oneByOneWidgetCount(widgets_);
        widgetPageIndex_ = itemCount <= 0 ? 0 : std::min(widgetPageIndex_, itemCount - 1);
      }
      if (supportsDescriptionSettings(widgets_[widgetSlot_])) {
        if (!wasDescription) {
          backgrounds_[widgetSlot_] = 0;
          descriptionTitles_[widgetSlot_] = 1;
          descriptionAuthors_[widgetSlot_] = 1;
          descriptionProgress_[widgetSlot_] = 1;
          descriptionRatings_[widgetSlot_] = 1;
        }
        carouselLabels_[widgetSlot_] = 0;
        carouselStyles_[widgetSlot_] = HomeTheme::CarouselStyle::Centered;
        carouselLabelColors_[widgetSlot_] = HomeTheme::CarouselLabelColor::Black;
        carouselShadowStyles_[widgetSlot_] = HomeTheme::CarouselShadowStyle::None;
      } else if (supportsLibrarySettings(widgets_[widgetSlot_])) {
        if (!wasLibrary) {
          setDefaultLibraryFolders(libraryFolders_[widgetSlot_]);
          backgrounds_[widgetSlot_] = 0;
          carouselLabels_[widgetSlot_] = 1;
          carouselStyles_[widgetSlot_] = HomeTheme::CarouselStyle::Centered;
          carouselLabelColors_[widgetSlot_] = HomeTheme::CarouselLabelColor::Black;
          carouselShadowStyles_[widgetSlot_] = HomeTheme::CarouselShadowStyle::None;
        }
      } else if (supportsCarouselSettings(widgets_[widgetSlot_])) {
        carouselStyles_[widgetSlot_] = HomeTheme::defaultCarouselStyle(widgets_[widgetSlot_]);
        carouselLabels_[widgetSlot_] = 1;
        carouselLabelColors_[widgetSlot_] = HomeTheme::CarouselLabelColor::Black;
        carouselShadowStyles_[widgetSlot_] = HomeTheme::CarouselShadowStyle::None;
        if (widgets_[widgetSlot_] == HomeTheme::Widget::Recent && !wasRecent) {
          backgrounds_[widgetSlot_] = 0;
          carouselStyles_[widgetSlot_] = HomeTheme::CarouselStyle::Left;
          carouselLabels_[widgetSlot_] = 0;
          carouselShadowStyles_[widgetSlot_] = HomeTheme::CarouselShadowStyle::Gray;
          recentTitles_[widgetSlot_] = 1;
          recentAuthors_[widgetSlot_] = 1;
          recentProgress_[widgetSlot_] = 1;
          recentRatings_[widgetSlot_] = 1;
        }
        if (widgets_[widgetSlot_] == HomeTheme::Widget::Carousel && !wasCarousel) {
          backgrounds_[widgetSlot_] = 0;
          carouselStyles_[widgetSlot_] = HomeTheme::CarouselStyle::Left;
          carouselLabels_[widgetSlot_] = 0;
          carouselShadowStyles_[widgetSlot_] = HomeTheme::CarouselShadowStyle::Gray;
          carouselProgress_[widgetSlot_] = 1;
        }
      } else if (supportsHeatmapSettings(widgets_[widgetSlot_])) {
        heatmapViews_[widgetSlot_] = HomeTheme::HeatmapView::Weekly;
        carouselLabels_[widgetSlot_] = 1;
        carouselLabelColors_[widgetSlot_] = HomeTheme::CarouselLabelColor::Black;
      } else if (supportsStreaksSettings(widgets_[widgetSlot_])) {
        if (!wasStreaks) {
          backgrounds_[widgetSlot_] = 0;
          streakCompact_[widgetSlot_] = 0;
        }
        carouselLabels_[widgetSlot_] = 0;
        carouselStyles_[widgetSlot_] = HomeTheme::CarouselStyle::Centered;
        carouselLabelColors_[widgetSlot_] = HomeTheme::CarouselLabelColor::Black;
        carouselShadowStyles_[widgetSlot_] = HomeTheme::CarouselShadowStyle::None;
      } else {
        carouselStyles_[widgetSlot_] = HomeTheme::CarouselStyle::Centered;
        carouselLabels_[widgetSlot_] = 0;
        carouselLabelColors_[widgetSlot_] = HomeTheme::CarouselLabelColor::Black;
        carouselShadowStyles_[widgetSlot_] = HomeTheme::CarouselShadowStyle::None;
      }
      widgetPopup_ = false;
      render();
      return;
    }
    const PreviewBounds preview = widgetPreviewBounds(renderer);
    const int previewX = preview.x;
    const int previewY = preview.y;
    const int previewW = preview.width;
    const int previewH = preview.height;
    const int columns = layoutColumns(layout_);
    const int cellW = previewW / columns;
    const int rows = layoutRows(layout_);
    const int cellH = previewH / rows;
    const int widgetPageCount = layout_ == HomeTheme::Layout::OneByOne ? oneByOneWidgetCount(widgets_) : 0;
    const int widgetPage = widgetPageCount <= 0 ? 0 : std::min(widgetPageCount - 1, std::max(0, widgetPageIndex_));
    const int firstSlot = layout_ == HomeTheme::Layout::OneByOne
                              ? oneByOneSlotAtPage(widgets_, widgetPage)
                              : 0;
    const int lastSlot = layout_ == HomeTheme::Layout::OneByOne ? firstSlot + 1 : HomeTheme::slotCount(layout_);
    if (layout_ == HomeTheme::Layout::OneByOne) {
      constexpr int containerRadius = 22;
      constexpr int edgeMargin = 10;
      const int centerY = previewY + previewH / 2;
      const int leftCenterX = edgeMargin + containerRadius;
      const int rightCenterX = renderer.getScreenWidth() - edgeMargin - containerRadius;
      const int optionCount = widgetOptionCount(layout_, editingSleep_);
      const int optionPage = optionCount <= 0 ? 0 : widgetOptionIndex(layout_, editingSleep_, widgets_[firstSlot]);
      if (optionPage > 0 && inside(x, y, leftCenterX - containerRadius, centerY - containerRadius,
                             containerRadius * 2, containerRadius * 2)) {
        cycleOneByOneWidget(-1);
      } else if (optionPage + 1 < optionCount &&
                 inside(x, y, rightCenterX - containerRadius, centerY - containerRadius,
                        containerRadius * 2, containerRadius * 2)) {
        cycleOneByOneWidget(1);
      }
      return;
    }
    for (int slot = firstSlot; slot < lastSlot; ++slot) {
      const int cellX = previewX + (slot % columns) * cellW;
      const int cellY = previewY + (slot / columns) * cellH;
      if (supportsCarouselSettings(widgets_[slot])) {
        const CarouselSettingsBounds settings = carouselSettingsBounds(cellX, cellY, cellW, cellH);
        if (inside(x, y, settings.x - 8, settings.y - 8, settings.size + 16, settings.size + 16)) {
          openCarouselSettings(slot);
          return;
        }
      }
      if (supportsHeatmapSettings(widgets_[slot])) {
        const CarouselSettingsBounds settings = carouselSettingsBounds(cellX, cellY, cellW, cellH);
        if (inside(x, y, settings.x - 8, settings.y - 8, settings.size + 16, settings.size + 16)) {
          openHeatmapSettings(slot);
          return;
        }
      }
#if FREEINK_DEVICE_STICKY
      if (supportsTemperatureSettings(widgets_[slot])) {
        const CarouselSettingsBounds settings = carouselSettingsBounds(cellX, cellY, cellW, cellH);
        if (inside(x, y, settings.x - 8, settings.y - 8, settings.size + 16, settings.size + 16)) {
          openTemperatureSettings(slot);
          return;
        }
      }
#endif
      if (supportsLibrarySettings(widgets_[slot])) {
        const CarouselSettingsBounds settings = carouselSettingsBounds(cellX, cellY, cellW, cellH);
        if (inside(x, y, settings.x - 8, settings.y - 8, settings.size + 16, settings.size + 16)) {
          openLibrarySettings(slot);
          return;
        }
      }
      if (supportsDescriptionSettings(widgets_[slot])) {
        const CarouselSettingsBounds settings = carouselSettingsBounds(cellX, cellY, cellW, cellH);
        if (inside(x, y, settings.x - 8, settings.y - 8, settings.size + 16, settings.size + 16)) {
          openDescriptionSettings(slot);
          return;
        }
      }
      if (supportsStatisticsSettings(widgets_[slot])) {
        const CarouselSettingsBounds settings = carouselSettingsBounds(cellX, cellY, cellW, cellH);
        if (inside(x, y, settings.x - 8, settings.y - 8, settings.size + 16, settings.size + 16)) {
          openStatisticsSettings(slot);
          return;
        }
      }
      if (supportsStreaksSettings(widgets_[slot])) {
        const CarouselSettingsBounds settings = carouselSettingsBounds(cellX, cellY, cellW, cellH);
        if (inside(x, y, settings.x - 8, settings.y - 8, settings.size + 16, settings.size + 16)) {
          openStreaksSettings(slot);
          return;
        }
      }
    }
    if (inside(x, y, previewX, previewY, previewW, previewH)) {
      const int column = std::min(columns - 1, (x - previewX) / cellW);
      const int row = std::min(rows - 1, (y - previewY) / cellH);
      const int slot = layout_ == HomeTheme::Layout::OneByOne ? firstSlot : row * columns + column;
      if (layout_ == HomeTheme::Layout::OneByThree) {
        for (int divider = 0; divider < 2; ++divider) {
          const BorderIconBounds icon = borderIconBounds(previewX, previewY + divider * cellH, previewW, cellH);
          if (inside(x, y, icon.x - 4, icon.y - 4, icon.size + 8, icon.size + 8)) {
            openBorderPopup(divider);
            return;
          }
        }
      }
      if (slot < HomeTheme::slotCount(layout_)) {
        const BorderIconBounds icon = borderIconBounds(previewX, previewY, previewW, cellH);
        if (inside(x, y, icon.x - 4, icon.y - 4, icon.size + 8, icon.size + 8)) {
          openBorderPopup(layout_ == HomeTheme::Layout::OneByOne ? firstSlot : 0);
        } else {
          openWidgetPopup(slot);
        }
      }
      return;
    }
  }
}

void ThemePickerActivity::loop() {
  if (subActivity) {
    subActivity->loop();
    if (carouselSettingsFinished_) {
      exitActivity();
      carouselSettingsFinished_ = false;
      render();
    }
    if (heatmapSettingsFinished_) {
      exitActivity();
      heatmapSettingsFinished_ = false;
      render();
    }
    if (temperatureSettingsFinished_) {
      exitActivity();
      temperatureSettingsFinished_ = false;
      render();
    }
    if (librarySettingsFinished_) {
      exitActivity();
      librarySettingsFinished_ = false;
      render();
    }
    if (descriptionSettingsFinished_) {
      exitActivity();
      descriptionSettingsFinished_ = false;
      render();
    }
    if (statisticsSettingsFinished_) {
      exitActivity();
      statisticsSettingsFinished_ = false;
      render();
    }
    if (streaksSettingsFinished_) {
      exitActivity();
      streaksSettingsFinished_ = false;
      render();
    }
    return;
  }
  if (screen_ == Screen::Widgets && widgetPopup_ && mappedInput.hasTouch()) {
    if (mappedInput.wasTouchSwipeUpForRenderer(renderer)) {
      pageWidgetPopup(1);
      render();
      return;
    }
    if (mappedInput.wasTouchSwipeDownForRenderer(renderer)) {
      pageWidgetPopup(-1);
      render();
      return;
    }
  }
  if (screen_ == Screen::Widgets && !widgetPopup_ && !borderPopup_ && mappedInput.hasTouch()) {
    const bool swipeLeft = mappedInput.wasTouchSwipeLeft() || mappedInput.wasTouchSwipeLeftForRenderer(renderer);
    const bool swipeRight = mappedInput.wasTouchSwipeRight() || mappedInput.wasTouchSwipeRightForRenderer(renderer);
    if (swipeLeft) {
      if (layout_ == HomeTheme::Layout::OneByOne) {
        cycleOneByOneWidget(1);
        return;
      }
    }
    if (swipeRight) {
      if (layout_ == HomeTheme::Layout::OneByOne) {
        cycleOneByOneWidget(-1);
        return;
      }
    }
  }
  if (!borderPopup_ && SubPage::closeInput(renderer, mappedInput, [this] {
        if (widgetPopup_) {
          widgetPopup_ = false;
          render();
        } else if (themeActionPopup_) {
          themeActionPopup_ = false;
          render();
        } else if (screen_ == Screen::Widgets) {
          saveEditorAndClose();
        } else if (screen_ == Screen::Layouts) {
          screen_ = Screen::List;
          render();
        } else {
          close();
        }
      })) {
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (widgetPopup_) {
      widgetPopup_ = false;
      render();
      return;
    }
    if (borderPopup_) {
      borderPopup_ = false;
      render();
      return;
    }
    if (themeActionPopup_) {
      themeActionPopup_ = false;
      render();
      return;
    }
    return;
  }
  if (mappedInput.hasTouch()) {
    float nx = 0.0f;
    float ny = 0.0f;
    if (mappedInput.wasTouchTapInScreen(renderer, nx, ny)) {
      const int tapX = static_cast<int>(nx * renderer.getScreenWidth());
      const int tapY = static_cast<int>(ny * renderer.getScreenHeight());
      handleTouch(tapX, tapY);
      return;
    }
  }
  if (screen_ == Screen::Mode) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Up) ||
        mappedInput.wasPressed(MappedInputManager::Button::Down)) {
      sleepMode_ = !sleepMode_;
      render();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      selected_ = sleepMode_ ? HomeTheme::sleepActiveIndex() : HomeTheme::activeIndex();
      listCursor_ = selected_ + 1;
      screen_ = Screen::List;
      render();
    }
    return;
  }
  if (screen_ == Screen::List) {
    if (themeActionPopup_) {
      if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
        moveThemeActionSelection(-1);
        render();
      } else if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
        moveThemeActionSelection(1);
        render();
      } else if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
        handleThemeAction(themeActionSelected_);
      }
      return;
    }
    const int themeCount = sleepMode_ ? HomeTheme::sleepCount() : HomeTheme::count();
    const int maxCursor = themeCount;
    if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
      listCursor_ = std::max(0, listCursor_ - 1);
      render();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
      listCursor_ = std::min(maxCursor, listCursor_ + 1);
      render();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      if (listCursor_ == 0) {
        beginNewTheme();
      } else {
        selected_ = listCursor_ - 1;
        themeActionSelected_ = 0;
        themeActionPopup_ = true;
        render();
      }
    }
    return;
  }
  if (screen_ == Screen::Layouts) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
      layoutSelection_ = std::max(0, layoutSelection_ - 1);
      render();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
      layoutSelection_ = std::min(2, layoutSelection_ + 1);
      render();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      applySelectedLayout();
    }
    return;
  }
  if (screen_ == Screen::Widgets) {
    if (borderPopup_) {
      const PopUpBounds box = PopUp::bounds(renderer, 4);
      if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
        borderPopupSelected_ = (borderPopupSelected_ + 3) % 4;
        render();
      } else if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
        borderPopupSelected_ = (borderPopupSelected_ + 1) % 4;
        render();
      } else if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
        borders_[widgetSlot_] = static_cast<HomeTheme::Border>(borderPopupSelected_);
        borderPopup_ = false;
        render();
      }
      (void)box;
      return;
    }
    if (widgetPopup_) {
      if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
        moveWidgetPopupSelection(-1);
        render();
      } else if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
        moveWidgetPopupSelection(1);
        render();
      } else if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
        const bool wasLibrary = widgets_[widgetSlot_] == HomeTheme::Widget::Library;
        const bool wasDescription = widgets_[widgetSlot_] == HomeTheme::Widget::Description;
        const bool wasRecent = widgets_[widgetSlot_] == HomeTheme::Widget::Recent;
        const bool wasCarousel = widgets_[widgetSlot_] == HomeTheme::Widget::Carousel;
        const bool wasStreaks = widgets_[widgetSlot_] == HomeTheme::Widget::Streaks;
        widgets_[widgetSlot_] = widgetOptionAt(layout_, editingSleep_, popupSelected_);
        if (widgets_[widgetSlot_] == HomeTheme::Widget::Empty) borders_[widgetSlot_] = HomeTheme::Border::None;
        if (layout_ == HomeTheme::Layout::OneByOne) {
          const int itemCount = oneByOneWidgetCount(widgets_);
          widgetPageIndex_ = itemCount <= 0 ? 0 : std::min(widgetPageIndex_, itemCount - 1);
        }
        if (supportsDescriptionSettings(widgets_[widgetSlot_])) {
          if (!wasDescription) {
            backgrounds_[widgetSlot_] = 0;
            descriptionTitles_[widgetSlot_] = 1;
            descriptionAuthors_[widgetSlot_] = 1;
            descriptionProgress_[widgetSlot_] = 1;
            descriptionRatings_[widgetSlot_] = 1;
          }
          carouselLabels_[widgetSlot_] = 0;
          carouselStyles_[widgetSlot_] = HomeTheme::CarouselStyle::Centered;
          carouselLabelColors_[widgetSlot_] = HomeTheme::CarouselLabelColor::Black;
          carouselShadowStyles_[widgetSlot_] = HomeTheme::CarouselShadowStyle::None;
        } else if (supportsLibrarySettings(widgets_[widgetSlot_])) {
          if (!wasLibrary) {
            setDefaultLibraryFolders(libraryFolders_[widgetSlot_]);
            backgrounds_[widgetSlot_] = 0;
            carouselLabels_[widgetSlot_] = 1;
            carouselStyles_[widgetSlot_] = HomeTheme::CarouselStyle::Centered;
            carouselLabelColors_[widgetSlot_] = HomeTheme::CarouselLabelColor::Black;
            carouselShadowStyles_[widgetSlot_] = HomeTheme::CarouselShadowStyle::None;
          }
        } else if (supportsCarouselSettings(widgets_[widgetSlot_])) {
          carouselStyles_[widgetSlot_] = HomeTheme::defaultCarouselStyle(widgets_[widgetSlot_]);
          carouselLabels_[widgetSlot_] = 1;
          carouselLabelColors_[widgetSlot_] = HomeTheme::CarouselLabelColor::Black;
          carouselShadowStyles_[widgetSlot_] = HomeTheme::CarouselShadowStyle::None;
          if (widgets_[widgetSlot_] == HomeTheme::Widget::Recent && !wasRecent) {
            backgrounds_[widgetSlot_] = 0;
            carouselStyles_[widgetSlot_] = HomeTheme::CarouselStyle::Left;
            carouselLabels_[widgetSlot_] = 0;
            carouselShadowStyles_[widgetSlot_] = HomeTheme::CarouselShadowStyle::Gray;
            recentTitles_[widgetSlot_] = 1;
            recentAuthors_[widgetSlot_] = 1;
            recentProgress_[widgetSlot_] = 1;
            recentRatings_[widgetSlot_] = 1;
          }
          if (widgets_[widgetSlot_] == HomeTheme::Widget::Carousel && !wasCarousel) {
            backgrounds_[widgetSlot_] = 0;
            carouselStyles_[widgetSlot_] = HomeTheme::CarouselStyle::Left;
            carouselLabels_[widgetSlot_] = 0;
            carouselShadowStyles_[widgetSlot_] = HomeTheme::CarouselShadowStyle::Gray;
            carouselProgress_[widgetSlot_] = 1;
          }
        } else if (supportsHeatmapSettings(widgets_[widgetSlot_])) {
          heatmapViews_[widgetSlot_] = HomeTheme::HeatmapView::Weekly;
          carouselLabels_[widgetSlot_] = 1;
          carouselLabelColors_[widgetSlot_] = HomeTheme::CarouselLabelColor::Black;
        } else if (supportsStreaksSettings(widgets_[widgetSlot_])) {
          if (!wasStreaks) {
            backgrounds_[widgetSlot_] = 0;
            streakCompact_[widgetSlot_] = 0;
          }
          carouselLabels_[widgetSlot_] = 0;
          carouselStyles_[widgetSlot_] = HomeTheme::CarouselStyle::Centered;
          carouselLabelColors_[widgetSlot_] = HomeTheme::CarouselLabelColor::Black;
          carouselShadowStyles_[widgetSlot_] = HomeTheme::CarouselShadowStyle::None;
        } else {
          carouselStyles_[widgetSlot_] = HomeTheme::CarouselStyle::Centered;
          carouselLabels_[widgetSlot_] = 0;
          carouselLabelColors_[widgetSlot_] = HomeTheme::CarouselLabelColor::Black;
          carouselShadowStyles_[widgetSlot_] = HomeTheme::CarouselShadowStyle::None;
        }
        widgetPopup_ = false;
        render();
      }
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
      if (layout_ == HomeTheme::Layout::OneByOne) {
        widgetPageIndex_ = std::max(0, widgetPageIndex_ - 1);
      } else {
        widgetSlot_ = std::max(0, widgetSlot_ - 1);
      }
      render();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
      if (layout_ == HomeTheme::Layout::OneByOne) {
        widgetPageIndex_ = std::min(std::max(0, oneByOneWidgetCount(widgets_) - 1), widgetPageIndex_ + 1);
      } else {
        widgetSlot_ = std::min(HomeTheme::slotCount(layout_) - 1, widgetSlot_ + 1);
      }
      render();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
      openWidgetPopup(layout_ == HomeTheme::Layout::OneByOne ? oneByOneSlotAtPage(widgets_, widgetPageIndex_) : widgetSlot_);
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
      openWidgetPopup(layout_ == HomeTheme::Layout::OneByOne ? oneByOneSlotAtPage(widgets_, widgetPageIndex_) : widgetSlot_);
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      saveEditorAndClose();
    }
  }
}
