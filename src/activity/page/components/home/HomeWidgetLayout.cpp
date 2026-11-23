#include "HomeWidgetLayout.h"

#include <GfxRenderer.h>

#include "../../navigation/Menu.h"

namespace {

int gridRows(const HomeTheme::Layout layout) {
  return layout == HomeTheme::Layout::OneByThree || layout == HomeTheme::Layout::HalfTop ||
                 layout == HomeTheme::Layout::HalfMiddle || layout == HomeTheme::Layout::HalfBottom ||
                 layout == HomeTheme::Layout::HalfTop50 || layout == HomeTheme::Layout::HalfMiddle50 ||
                 layout == HomeTheme::Layout::HalfBottom50 || layout == HomeTheme::Layout::HalfTopMiddle40 ||
                 layout == HomeTheme::Layout::HalfTopBottom40 || layout == HomeTheme::Layout::HalfMiddleBottom40 ||
                 layout == HomeTheme::Layout::HalfTop50Middle30 || layout == HomeTheme::Layout::HalfTop50Bottom30 ||
                 layout == HomeTheme::Layout::HalfMiddle50Top30 || layout == HomeTheme::Layout::HalfMiddle50Bottom30 ||
                 layout == HomeTheme::Layout::HalfBottom50Top30 || layout == HomeTheme::Layout::HalfBottom50Middle30
             ? 3
             : 2;
}

bool isHalfStackLayout(const HomeTheme::Layout layout) {
  return layout == HomeTheme::Layout::HalfTop || layout == HomeTheme::Layout::HalfMiddle ||
         layout == HomeTheme::Layout::HalfBottom || layout == HomeTheme::Layout::HalfTop50 ||
         layout == HomeTheme::Layout::HalfMiddle50 || layout == HomeTheme::Layout::HalfBottom50 ||
         layout == HomeTheme::Layout::HalfTopMiddle40 || layout == HomeTheme::Layout::HalfTopBottom40 ||
         layout == HomeTheme::Layout::HalfMiddleBottom40 ||
         layout == HomeTheme::Layout::HalfTop50Middle30 || layout == HomeTheme::Layout::HalfTop50Bottom30 ||
         layout == HomeTheme::Layout::HalfMiddle50Top30 || layout == HomeTheme::Layout::HalfMiddle50Bottom30 ||
         layout == HomeTheme::Layout::HalfBottom50Top30 || layout == HomeTheme::Layout::HalfBottom50Middle30;
}

int halfStackRowPercent(const HomeTheme::Layout layout, const int row) {
  if (layout == HomeTheme::Layout::HalfTop50) return row == 0 ? 50 : 25;
  if (layout == HomeTheme::Layout::HalfMiddle50) return row == 1 ? 50 : 25;
  if (layout == HomeTheme::Layout::HalfBottom50) return row == 2 ? 50 : 25;
  if (layout == HomeTheme::Layout::HalfTop50Middle30) return row == 0 ? 50 : row == 1 ? 30 : 20;
  if (layout == HomeTheme::Layout::HalfTop50Bottom30) return row == 0 ? 50 : row == 1 ? 20 : 30;
  if (layout == HomeTheme::Layout::HalfMiddle50Top30) return row == 0 ? 30 : row == 1 ? 50 : 20;
  if (layout == HomeTheme::Layout::HalfMiddle50Bottom30) return row == 0 ? 20 : row == 1 ? 50 : 30;
  if (layout == HomeTheme::Layout::HalfBottom50Top30) return row == 0 ? 30 : row == 1 ? 20 : 50;
  if (layout == HomeTheme::Layout::HalfBottom50Middle30) return row == 0 ? 20 : row == 1 ? 30 : 50;
  if (layout == HomeTheme::Layout::HalfTopMiddle40) return row < 2 ? 40 : 20;
  if (layout == HomeTheme::Layout::HalfTopBottom40) return row == 1 ? 20 : 40;
  if (layout == HomeTheme::Layout::HalfMiddleBottom40) return row == 0 ? 20 : 40;
  if (layout == HomeTheme::Layout::HalfTop) return row == 0 ? 40 : 30;
  if (layout == HomeTheme::Layout::HalfMiddle) return row == 1 ? 40 : 30;
  if (layout == HomeTheme::Layout::HalfBottom) return row == 2 ? 40 : 30;
  return 0;
}

bool isCompactLayout(const HomeTheme::Layout layout) {
  return layout == HomeTheme::Layout::OneByThree || isHalfStackLayout(layout);
}

void halfStackRowHeights(const HomeTheme::Layout layout, const int areaH, const int gap, int heights[3]) {
  const int availableHeight = std::max(0, areaH - gap * 2);
  const int bottomInset = navigation::Menu::bottomInset();
  const bool bottomMenuPresent = isHalfStackLayout(layout) && bottomInset > 0;
  const int baseHeight = availableHeight + (bottomMenuPresent ? bottomInset : 0);

  heights[0] = baseHeight * halfStackRowPercent(layout, 0) / 100;
  heights[1] = baseHeight * halfStackRowPercent(layout, 1) / 100;
  heights[2] = baseHeight - heights[0] - heights[1];

  if (!bottomMenuPresent) return;



  const int smallestPercent = std::min({halfStackRowPercent(layout, 0), halfStackRowPercent(layout, 1),
                                        halfStackRowPercent(layout, 2)});
  int largeRowCount = 0;
  for (int index = 0; index < 3; ++index) {
    if (halfStackRowPercent(layout, index) > smallestPercent) ++largeRowCount;
  }

  int remainingInset = bottomInset;
  int remainingLargeRows = largeRowCount;
  for (int index = 0; index < 3; ++index) {
    if (halfStackRowPercent(layout, index) <= smallestPercent) continue;
    const int reduction = (remainingInset + remainingLargeRows - 1) / remainingLargeRows;
    heights[index] = std::max(0, heights[index] - reduction);
    remainingInset -= reduction;
    --remainingLargeRows;
  }
}

int halfStackRowHeight(const HomeTheme::Layout layout, const int areaH, const int gap, const int row) {
  int heights[3] = {0, 0, 0};
  halfStackRowHeights(layout, areaH, gap, heights);
  return heights[std::min(2, std::max(0, row))];
}

int halfStackRowY(const HomeTheme::Layout layout, const int areaY, const int areaH, const int gap, const int row) {
  int y = areaY;
  for (int previous = 0; previous < row; ++previous) {
    y += halfStackRowHeight(layout, areaH, gap, previous) + gap;
  }
  return y;
}

bool isMostCompactRow(const HomeTheme::Layout layout, const int slot) {
  return isHalfStackLayout(layout) && halfStackRowPercent(layout, slot) <= 25;
}

bool isHalfStack50Row(const HomeTheme::Layout layout, const int slot) {
  return isHalfStackLayout(layout) && halfStackRowPercent(layout, slot) == 50;
}




bool useOneByTwoWidgetLayout(const HomeTheme::Layout layout, const int slot) {
  return isCompactLayout(layout) && !isHalfStack50Row(layout, slot);
}

bool useLargeCenterCarousel(const HomeTheme::Theme& theme, const int slot,
                            const HomeTheme::CarouselStyle style) {
  if (style != HomeTheme::CarouselStyle::Centered) return false;
  return (isHalfStackLayout(theme.layout) && halfStackRowPercent(theme.layout, slot) == 50) ||
         (theme.layout == HomeTheme::Layout::OneByTwo && SETTINGS.hiddenMenu != 0);
}




bool useFourBookCarousel(const HomeTheme::Theme& theme, const int slot) {
  return isCompactLayout(theme.layout) &&
         !useLargeCenterCarousel(theme, slot, theme.carouselStyles[slot]);
}

}

HomeWidgetLayout::HomeWidgetLayout(GfxRenderer& renderer)
    : renderer_(renderer),
      carousel_(renderer),
      clock_(renderer),
      calendar_(renderer),
      recent_(renderer),
      recentGrid_(renderer),
      flow_(renderer),
      verticalBookshelf_(),
      shortcut_(renderer),
      temperature_(renderer),
      humidity_(renderer),
      imageWidget_(renderer),
      todaysReading_(renderer),
      weeklyStatistics_(renderer),
      streaks_(renderer),
      favorites_(renderer),
      heatmap_(renderer),
      library_(renderer),
      description_(renderer) {}

void HomeWidgetLayout::render(const HomeTheme::Theme& theme, const int carouselIndex, const int favoriteIndex,
                              const int widgetPageIndex, const int recentGridPageIndex, const int flowIndex) const {
  switch (theme.layout) {
    case HomeTheme::Layout::Classic:
      renderClassic(carouselIndex);
      return;
    case HomeTheme::Layout::OneByTwo:
    case HomeTheme::Layout::OneByThree:
    case HomeTheme::Layout::TwoByTwo:
    case HomeTheme::Layout::HalfTop:
    case HomeTheme::Layout::HalfMiddle:
    case HomeTheme::Layout::HalfBottom:
    case HomeTheme::Layout::HalfTop50:
    case HomeTheme::Layout::HalfMiddle50:
    case HomeTheme::Layout::HalfBottom50:
    case HomeTheme::Layout::HalfTopMiddle40:
    case HomeTheme::Layout::HalfTopBottom40:
    case HomeTheme::Layout::HalfMiddleBottom40:
    case HomeTheme::Layout::HalfTop50Middle30:
    case HomeTheme::Layout::HalfTop50Bottom30:
    case HomeTheme::Layout::HalfMiddle50Top30:
    case HomeTheme::Layout::HalfMiddle50Bottom30:
    case HomeTheme::Layout::HalfBottom50Top30:
    case HomeTheme::Layout::HalfBottom50Middle30:
      renderGrid(theme, carouselIndex, favoriteIndex);
      return;
    case HomeTheme::Layout::OneByOne:
      renderOneByOne(theme, carouselIndex, favoriteIndex, widgetPageIndex, recentGridPageIndex, flowIndex);
      return;
  }
}

void HomeWidgetLayout::preloadCarousel(const HomeTheme::Theme& theme, const int carouselIndex) const {
  if (theme.layout == HomeTheme::Layout::Classic) {
    carousel_.preload(carouselIndex, 0, navigation::Menu::height, renderer_.getScreenWidth(), Carousel::kHeight,
                      HomeTheme::CarouselStyle::Centered);
    return;
  }

  const Grid layout = grid(theme.layout);
  const bool hideFirst = hideFirstCarouselBook(theme);
  for (int slot = 0; slot < HomeTheme::slotCount(theme.layout); ++slot) {
    if (theme.widgets[slot] != HomeTheme::Widget::Carousel) continue;
    const Bounds bounds = slotBounds(layout, slot);
    carousel_.preload(carouselIndex, bounds.x, bounds.y, bounds.width, bounds.height, theme.carouselStyles[slot],
                      theme.carouselLabels[slot] != 0, theme.carouselLabelColors[slot], hideFirst,
                      theme.carouselFullHeight[slot] != 0, useFourBookCarousel(theme, slot),
                      theme.layout == HomeTheme::Layout::OneByTwo && SETTINGS.hiddenMenu != 0,
                      useLargeCenterCarousel(theme, slot, theme.carouselStyles[slot]));
  }
}

void HomeWidgetLayout::invalidateFavorites() const { favorites_.invalidate(); }

bool HomeWidgetLayout::needsRefresh(const HomeTheme::Theme& theme) const {
  for (int slot = 0; slot < HomeTheme::slotCount(theme.layout); ++slot) {
    if (theme.widgets[slot] == HomeTheme::Widget::Clock && clock_.needsRefresh()) return true;
    if (theme.widgets[slot] == HomeTheme::Widget::Calendar && calendar_.needsRefresh()) return true;
#if FREEINK_DEVICE_STICKY
    if (theme.widgets[slot] == HomeTheme::Widget::Temperature && temperature_.needsRefresh()) return true;
    if (theme.widgets[slot] == HomeTheme::Widget::Humidity && humidity_.needsRefresh()) return true;
#endif
    if (theme.widgets[slot] == HomeTheme::Widget::TodaysReading && todaysReading_.needsRefresh()) return true;
    if (theme.widgets[slot] == HomeTheme::Widget::Heatmap && heatmap_.needsRefresh(HomeTheme::HeatmapView::Monthly))
      return true;
    if (theme.widgets[slot] == HomeTheme::Widget::Streaks && streaks_.needsRefresh()) return true;
  }
  return false;
}

int HomeWidgetLayout::carouselBookCount(const HomeTheme::Theme& theme, const int bookCount) const {
  if (bookCount <= 0) return 0;
  return std::max(0, bookCount - (hideFirstCarouselBook(theme) ? 1 : 0));
}

void HomeWidgetLayout::widgetBounds(const HomeTheme::Layout layout, const int slot, const bool sleep, int& x, int& y,
                                    int& width, int& height, const HomeTheme::Theme* theme) const {
  if (layout == HomeTheme::Layout::OneByOne) {
    x = 0;
    y = navigation::Menu::height;
    width = renderer_.getScreenWidth();
    height = renderer_.getScreenHeight() - navigation::Menu::height - navigation::Menu::bottomInset();
    return;
  }
  const Bounds bounds = slotBounds(grid(layout, sleep, theme), slot);
  x = bounds.x;
  y = bounds.y;
  width = bounds.width;
  height = bounds.height;
}

void HomeWidgetLayout::widgetSize(const HomeTheme::Layout layout, const int slot, const bool sleep, int& width,
                                  int& height, const HomeTheme::Theme* theme) const {
  int x = 0;
  int y = 0;
  widgetBounds(layout, slot, sleep, x, y, width, height, theme);
}

void HomeWidgetLayout::renderSleep(const HomeTheme::Theme& theme) const {
  if (theme.layout == HomeTheme::Layout::Classic) return;
  if (theme.layout == HomeTheme::Layout::OneByOne) {
    renderOneByOne(theme, 0, 0, 0, 0, 0);
  } else {
    renderGrid(theme, 0, 0, true);
  }
}

HomeWidgetLayout::Grid HomeWidgetLayout::grid(const HomeTheme::Layout layout, const bool sleep,
                                              const HomeTheme::Theme* theme) const {
  constexpr int gap = 0;
  const int baseColumns = layout == HomeTheme::Layout::TwoByTwo ? 2 : 1;
  const int baseRows = gridRows(layout);
  const int margin = 0;
  const int areaY = navigation::Menu::height;
  const int fullWidth = renderer_.getScreenWidth();
  const int fullHeight = renderer_.getScreenHeight() - areaY - navigation::Menu::bottomInset();

  int columns = baseColumns;
  int rows = baseRows;
  int slotColumnOffset = 0;
  int slotRowOffset = 0;
  int centeredX = margin;
  int centeredY = areaY;
  int centeredWidth = fullWidth;
  int centeredHeight = fullHeight;

  if (sleep && theme != nullptr) {
    int minColumn = baseColumns;
    int minRow = baseRows;
    int maxColumn = -1;
    int maxRow = -1;
    for (int slot = 0; slot < HomeTheme::slotCount(layout); ++slot) {
      if (theme->widgets[slot] == HomeTheme::Widget::Empty) continue;
      const int column = slot % baseColumns;
      const int row = slot / baseColumns;
      minColumn = std::min(minColumn, column);
      minRow = std::min(minRow, row);
      maxColumn = std::max(maxColumn, column);
      maxRow = std::max(maxRow, row);
    }

    if (maxColumn >= 0 && maxRow >= 0) {
      const int baseCellWidth = (fullWidth - gap * (baseColumns - 1)) / baseColumns;
      const int baseCellHeight = (fullHeight - gap * (baseRows - 1)) / baseRows;
      columns = maxColumn - minColumn + 1;
      rows = maxRow - minRow + 1;
      slotColumnOffset = minColumn;
      slotRowOffset = minRow;
      centeredWidth = baseCellWidth * columns + gap * (columns - 1);
      centeredHeight = baseCellHeight * rows + gap * (rows - 1);
      centeredX = margin + (fullWidth - centeredWidth) / 2;
      centeredY = areaY + (fullHeight - centeredHeight) / 2;
    }
  }

  return {columns,        rows, centeredX,   centeredY,        centeredWidth,
          centeredHeight, gap,  baseColumns, slotColumnOffset, slotRowOffset, layout};
}

HomeWidgetLayout::Bounds HomeWidgetLayout::slotBounds(const Grid& layout, const int slot) const {
  if (isHalfStackLayout(layout.layout)) {
    const int row = std::min(2, std::max(0, slot));
    return {layout.areaX, halfStackRowY(layout.layout, layout.areaY, layout.areaH, layout.gap, row), layout.areaW,
            halfStackRowHeight(layout.layout, layout.areaH, layout.gap, row)};
  }
  const int cellWidth = (layout.areaW - layout.gap * (layout.columns - 1)) / layout.columns;
  const int cellHeight = (layout.areaH - layout.gap * (layout.rows - 1)) / layout.rows;
  const int column = slot % layout.slotColumns - layout.slotColumnOffset;
  const int row = slot / layout.slotColumns - layout.slotRowOffset;
  return {layout.areaX + column * (cellWidth + layout.gap), layout.areaY + row * (cellHeight + layout.gap), cellWidth,
          cellHeight};
}

void HomeWidgetLayout::renderClassic(const int carouselIndex) const {
  carousel_.render(carouselIndex, 0, navigation::Menu::height, renderer_.getScreenWidth(), Carousel::kHeight);
  shortcut_.render(
      20, navigation::Menu::height + Carousel::kHeight + 20, renderer_.getScreenWidth() - 40,
      renderer_.getScreenHeight() - navigation::Menu::height - Carousel::kHeight - navigation::Menu::bottomInset() - 40);
}

void HomeWidgetLayout::renderGrid(const HomeTheme::Theme& theme, const int carouselIndex, const int favoriteIndex,
                                  const bool sleep) const {
  const Grid layout = grid(theme.layout, sleep, sleep ? &theme : nullptr);
  const bool hideFirst = hideFirstCarouselBook(theme);
  int descriptionRecentIndex = -1;
  for (int slot = 0; slot < HomeTheme::slotCount(theme.layout); ++slot) {
    if (theme.widgets[slot] == HomeTheme::Widget::Carousel) {
      descriptionRecentIndex = hideFirst ? carouselIndex + 1 : carouselIndex;
      break;
    }
    if (descriptionRecentIndex < 0 && theme.widgets[slot] == HomeTheme::Widget::Recent) {
      descriptionRecentIndex = 0;
    }
  }
  for (int slot = 0; slot < HomeTheme::slotCount(theme.layout); ++slot) {
    const Bounds bounds = slotBounds(layout, slot);
    renderSlot(theme, slot, bounds, carouselIndex, favoriteIndex, descriptionRecentIndex, hideFirst, 0, 0);
  }
  renderBorders(theme, layout);
}

void HomeWidgetLayout::renderOneByOne(const HomeTheme::Theme& theme, const int carouselIndex,
                                      const int favoriteIndex, const int widgetPageIndex,
                                      const int recentGridPageIndex, const int flowIndex) const {
  const int slot = oneByOnePageCount(theme) == 0 ? 0 : [&] {
    int page = std::max(0, widgetPageIndex);
    for (int i = 0; i < HomeTheme::slotCount(theme.layout); ++i) {
      if (theme.widgets[i] == HomeTheme::Widget::Empty) continue;
      if (page-- == 0) return i;
    }
    for (int i = 0; i < HomeTheme::slotCount(theme.layout); ++i) {
      if (theme.widgets[i] != HomeTheme::Widget::Empty) return i;
    }
    return 0;
  }();
  const Bounds bounds{0, navigation::Menu::height, renderer_.getScreenWidth(),
                      renderer_.getScreenHeight() - navigation::Menu::height - navigation::Menu::bottomInset()};
  const bool hideFirst = hideFirstCarouselBook(theme);
  int descriptionRecentIndex = hideFirst ? carouselIndex + 1 : carouselIndex;
  for (int i = 0; i < HomeTheme::slotCount(theme.layout); ++i) {
    if (theme.widgets[i] == HomeTheme::Widget::Recent) {
      descriptionRecentIndex = 0;
      break;
    }
  }
  renderSlot(theme, slot, bounds, carouselIndex, favoriteIndex, descriptionRecentIndex, hideFirst,
             recentGridPageIndex, flowIndex);
  const Grid borderLayout{1, 1, bounds.x, bounds.y, bounds.width, bounds.height, 0, 1, 0, 0};
  renderBorder(theme.borders[slot], borderLayout);
}

int HomeWidgetLayout::oneByOnePageCount(const HomeTheme::Theme& theme) const {
  int count = 0;
  for (int slot = 0; slot < HomeTheme::slotCount(theme.layout); ++slot) {
    if (theme.widgets[slot] != HomeTheme::Widget::Empty) ++count;
  }
  return count;
}

int HomeWidgetLayout::recentGridPageCount(const HomeTheme::Theme& theme, const int widgetPageIndex,
                                          const int bookCount) const {
  if (theme.layout != HomeTheme::Layout::OneByOne || bookCount <= 0) return 0;
  int page = std::max(0, widgetPageIndex);
  for (int slot = 0; slot < HomeTheme::slotCount(theme.layout); ++slot) {
    if (theme.widgets[slot] == HomeTheme::Widget::Empty) continue;
    if (page-- == 0) {
      if (theme.widgets[slot] != HomeTheme::Widget::RecentGrid) return 0;
      const int remainingBookCount = std::max(0, bookCount - 1);
      return (remainingBookCount + RecentGrid::kPageSize - 1) / RecentGrid::kPageSize;
    }
  }
  return 0;
}

void HomeWidgetLayout::renderSlot(const HomeTheme::Theme& theme, const int slot, const Bounds& bounds,
                                  const int carouselIndex, const int favoriteIndex, const int descriptionRecentIndex,
                                  const bool hideFirst, const int recentGridPageIndex, const int flowIndex) const {
  const bool compactWidgetLayout = useOneByTwoWidgetLayout(theme.layout, slot);
  const bool mostCompactRow = isMostCompactRow(theme.layout, slot);
  switch (theme.widgets[slot]) {
    case HomeTheme::Widget::Carousel:
      carousel_.render(carouselIndex, bounds.x, bounds.y, bounds.width, bounds.height, theme.backgrounds[slot] != 0,
                       theme.carouselStyles[slot], theme.carouselLabels[slot] != 0, theme.carouselLabelColors[slot],
                       theme.carouselShadowStyles[slot], theme.carouselProgress[slot] != 0, hideFirst,
                       theme.carouselFullHeight[slot] != 0, useFourBookCarousel(theme, slot),
                       theme.layout == HomeTheme::Layout::OneByTwo && SETTINGS.hiddenMenu != 0,
                       useLargeCenterCarousel(theme, slot, theme.carouselStyles[slot]));
      break;
    case HomeTheme::Widget::Shortcuts:
      shortcut_.render(bounds.x, bounds.y, bounds.width, bounds.height, compactWidgetLayout, theme.shortcutItems);
      break;
    case HomeTheme::Widget::Clock:
      clock_.render(bounds.x, bounds.y, bounds.width, bounds.height, compactWidgetLayout, mostCompactRow);
      break;
    case HomeTheme::Widget::Calendar:
      calendar_.render(bounds.x, bounds.y, bounds.width, bounds.height, compactWidgetLayout);
      break;
    case HomeTheme::Widget::Recent:
      recent_.render(bounds.x, bounds.y, bounds.width, bounds.height, theme.backgrounds[slot] != 0,
                     theme.carouselStyles[slot], theme.carouselLabels[slot] != 0, theme.carouselLabelColors[slot],
                     theme.carouselShadowStyles[slot], theme.recentTitles[slot] != 0, theme.recentAuthors[slot] != 0,
                     theme.recentProgress[slot] != 0, theme.recentRatings[slot] != 0,
                     theme.carouselFullHeight[slot] != 0);
      break;
    case HomeTheme::Widget::RecentGrid:
      recentGrid_.render(bounds.x, bounds.y, bounds.width, bounds.height, recentGridPageIndex);
      break;
    case HomeTheme::Widget::Flow:
      flow_.render(bounds.x, bounds.y, bounds.width, bounds.height, flowIndex,
                   theme.layout == HomeTheme::Layout::OneByOne);
      break;
    case HomeTheme::Widget::VerticalBookshelf:
      verticalBookshelf_.render(renderer_, bounds.x, bounds.y, bounds.width, bounds.height, flowIndex, false);
      break;
#if FREEINK_DEVICE_STICKY
    case HomeTheme::Widget::Temperature: temperature_.render(bounds.x, bounds.y, bounds.width, bounds.height); break;
    case HomeTheme::Widget::Humidity: humidity_.render(bounds.x, bounds.y, bounds.width, bounds.height); break;
#endif
    case HomeTheme::Widget::TodaysReading:
      todaysReading_.render(bounds.x, bounds.y, bounds.width, bounds.height, compactWidgetLayout, mostCompactRow,
                            theme.layout == HomeTheme::Layout::OneByThree ||
                                (isHalfStackLayout(theme.layout) && halfStackRowPercent(theme.layout, slot) == 30));
      break;
    case HomeTheme::Widget::Statistics:
      weeklyStatistics_.render(bounds.x, bounds.y, bounds.width, bounds.height, compactWidgetLayout,
                               theme.carouselStyles[slot] == HomeTheme::CarouselStyle::Left,
                               theme.statisticsSummary[slot] != 0);
      break;
    case HomeTheme::Widget::Image:
      imageWidget_.render(bounds.x, bounds.y, bounds.width, bounds.height, theme.imagePaths[slot],
                          theme.imageCropToFill[slot] != 0, theme.imagePadding[slot] != 0,
                          theme.imageCropAnchorX[slot] / 255.0f, theme.imageCropAnchorY[slot] / 255.0f);
      break;
    case HomeTheme::Widget::Streaks:
      {
        const bool compact = isHalfStack50Row(theme.layout, slot)
                                 ? false
                                 : compactWidgetLayout || theme.streakCompact[slot] != 0;
        const bool monthly = !compact &&
                             (theme.layout == HomeTheme::Layout::OneByTwo ||
                              isHalfStack50Row(theme.layout, slot));
        streaks_.render(bounds.x, bounds.y, bounds.width, bounds.height,
                        monthly, compact,
                        mostCompactRow);
      }
      break;
    case HomeTheme::Widget::Favorites:
      favorites_.render(favoriteIndex, bounds.x, bounds.y, bounds.width, bounds.height, theme.backgrounds[slot] != 0,
                        theme.carouselStyles[slot], theme.carouselLabels[slot] != 0, theme.carouselLabelColors[slot],
                        theme.carouselShadowStyles[slot], theme.carouselFullHeight[slot] != 0,
                        compactWidgetLayout);
      break;
    case HomeTheme::Widget::Heatmap:
      {



        const bool heatmapOneByTwo = theme.layout == HomeTheme::Layout::OneByTwo ||
                                     isHalfStack50Row(theme.layout, slot);
        const bool heatmapCompact = compactWidgetLayout || heatmapOneByTwo;
      heatmap_.render(bounds.x, bounds.y, bounds.width, bounds.height, HomeTheme::HeatmapView::Monthly, false,
                      theme.carouselLabelColors[slot], heatmapCompact,
                      compactWidgetLayout && !mostCompactRow, mostCompactRow,
                      theme.layout == HomeTheme::Layout::OneByThree && navigation::Menu::bottomInset() > 0,
                      theme.layout == HomeTheme::Layout::OneByThree ||
                          (isHalfStackLayout(theme.layout) && halfStackRowPercent(theme.layout, slot) < 40));
      }
      break;
    case HomeTheme::Widget::Library:
      library_.render(bounds.x, bounds.y, bounds.width, bounds.height, theme.libraryFolders[slot],
                      theme.backgrounds[slot] != 0, theme.carouselLabels[slot] != 0,
                      compactWidgetLayout);
      break;
    case HomeTheme::Widget::Description:
      description_.render(descriptionRecentIndex, bounds.x, bounds.y, bounds.width, bounds.height,
                          theme.backgrounds[slot] != 0, theme.carouselLabels[slot] != 0,
                          theme.carouselLabelColors[slot], theme.carouselShadowStyles[slot],
                          theme.descriptionTitles[slot] != 0, theme.descriptionAuthors[slot] != 0,
                          theme.descriptionProgress[slot] != 0, theme.descriptionRatings[slot] != 0,
                          compactWidgetLayout);
      break;
    case HomeTheme::Widget::Empty:
    default: break;
  }
}

void HomeWidgetLayout::renderBorders(const HomeTheme::Theme& theme, const Grid& layout) const {
  if (isHalfStackLayout(theme.layout)) {
    for (int row = 1; row < 3; ++row) {
      const HomeTheme::Border border = theme.borders[row - 1];
      const int dividerY = halfStackRowY(layout.layout, layout.areaY, layout.areaH, layout.gap, row);
      switch (border) {
        case HomeTheme::Border::Subtle:
          renderer_.line.render(layout.areaX, dividerY, layout.areaX + layout.areaW, dividerY, true,
                                LineRender::Style::Dotted);
          break;
        case HomeTheme::Border::Normal:
          renderer_.line.render(layout.areaX, dividerY, layout.areaX + layout.areaW, dividerY, true);
          break;
        case HomeTheme::Border::Thick:
          for (int offset = -1; offset <= 1; ++offset) {
            renderer_.line.render(layout.areaX, dividerY + offset, layout.areaX + layout.areaW, dividerY + offset,
                                  true);
          }
          break;
        case HomeTheme::Border::None:
        default:
          break;
      }
    }
    return;
  }
  if (theme.layout == HomeTheme::Layout::OneByThree && layout.columns == 1 && layout.rows == 3) {
    const int cellHeight = (layout.areaH - layout.gap * (layout.rows - 1)) / layout.rows;
    for (int row = 1; row < layout.rows; ++row) {
      const HomeTheme::Border border = theme.borders[row - 1];
      const int dividerY = layout.areaY + row * cellHeight;
      switch (border) {
        case HomeTheme::Border::Subtle:
          renderer_.line.render(layout.areaX, dividerY, layout.areaX + layout.areaW, dividerY, true,
                                LineRender::Style::Dotted);
          break;
        case HomeTheme::Border::Normal:
          renderer_.line.render(layout.areaX, dividerY, layout.areaX + layout.areaW, dividerY, true);
          break;
        case HomeTheme::Border::Thick:
          for (int offset = -1; offset <= 1; ++offset) {
            renderer_.line.render(layout.areaX, dividerY + offset, layout.areaX + layout.areaW, dividerY + offset,
                                  true);
          }
          break;
        case HomeTheme::Border::None:
        default:
          break;
      }
    }
    return;
  }

  renderBorder(theme.borders[0], layout);
}

void HomeWidgetLayout::renderBorder(const HomeTheme::Border border, const Grid& layout) const {
  if (layout.areaW <= 0 || layout.areaH <= 0) return;
  const int cellWidth = (layout.areaW - layout.gap * (layout.columns - 1)) / layout.columns;
  const int cellHeight = (layout.areaH - layout.gap * (layout.rows - 1)) / layout.rows;
  const int dividerY = layout.areaY + cellHeight;
  const int dividerX = layout.areaX + cellWidth;
  switch (border) {
    case HomeTheme::Border::Subtle:
      if (layout.rows > 1) {
        renderer_.line.render(layout.areaX, dividerY, layout.areaX + layout.areaW, dividerY, true,
                              LineRender::Style::Dotted);
      }
      if (layout.columns == 2) {
        renderer_.line.render(dividerX, layout.areaY, dividerX, layout.areaY + layout.areaH, true,
                              LineRender::Style::Dotted);
      }
      break;
    case HomeTheme::Border::Normal:
      if (layout.rows > 1) renderer_.line.render(layout.areaX, dividerY, layout.areaX + layout.areaW, dividerY, true);
      if (layout.columns == 2)
        renderer_.line.render(dividerX, layout.areaY, dividerX, layout.areaY + layout.areaH, true);
      break;
    case HomeTheme::Border::Thick:
      for (int offset = -1; offset <= 1; ++offset) {
        if (layout.rows > 1) {
          renderer_.line.render(layout.areaX, dividerY + offset, layout.areaX + layout.areaW, dividerY + offset, true);
        }
        if (layout.columns == 2) {
          renderer_.line.render(dividerX + offset, layout.areaY, dividerX + offset, layout.areaY + layout.areaH, true);
        }
      }
      break;
    case HomeTheme::Border::None:
    default:
      break;
  }
}

int HomeWidgetLayout::findWidget(const HomeTheme::Theme& theme, const HomeTheme::Widget widget, const int x,
                                 const int y) const {
  if (theme.layout == HomeTheme::Layout::Classic) return -1;
  if (theme.layout == HomeTheme::Layout::OneByOne) {
    for (int slot = 0; slot < HomeTheme::slotCount(theme.layout); ++slot) {
      if (theme.widgets[slot] == HomeTheme::Widget::Empty || theme.widgets[slot] != widget) continue;
      const int areaY = navigation::Menu::height;
      const int areaH = renderer_.getScreenHeight() - areaY - navigation::Menu::bottomInset();
      if (x >= 0 && x < renderer_.getScreenWidth() && y >= areaY && y < areaY + areaH) return slot;
    }
    return -1;
  }
  const Grid layout = grid(theme.layout);
  for (int slot = 0; slot < HomeTheme::slotCount(theme.layout); ++slot) {
    if (theme.widgets[slot] != widget) continue;
    const Bounds bounds = slotBounds(layout, slot);
    if (x >= bounds.x && x < bounds.x + bounds.width && y >= bounds.y && y < bounds.y + bounds.height) return slot;
  }
  return -1;
}

int HomeWidgetLayout::carouselAt(const HomeTheme::Theme& theme, const int carouselIndex, const int count, const int x,
                                 const int y) const {
  const bool hideFirst = hideFirstCarouselBook(theme);
  if (theme.layout == HomeTheme::Layout::OneByOne) return -1;
  if (theme.layout == HomeTheme::Layout::Classic) {
    return carousel_.hitTest(carouselIndex, count, x, y, 0, navigation::Menu::height, renderer_.getScreenWidth(),
                             Carousel::kHeight, HomeTheme::CarouselStyle::Centered, true);
  }
  const int slot = findWidget(theme, HomeTheme::Widget::Carousel, x, y);
  if (slot < 0) return -1;
  const Bounds bounds = slotBounds(grid(theme.layout), slot);
  return carousel_.hitTest(carouselIndex, count, x, y, bounds.x, bounds.y, bounds.width, bounds.height,
                           theme.carouselStyles[slot], theme.carouselLabels[slot] != 0, theme.carouselLabelColors[slot],
                           hideFirst, theme.carouselFullHeight[slot] != 0,
                           useFourBookCarousel(theme, slot),
                           theme.layout == HomeTheme::Layout::OneByTwo && SETTINGS.hiddenMenu != 0,
                           useLargeCenterCarousel(theme, slot, theme.carouselStyles[slot]));
}

bool HomeWidgetLayout::hideFirstCarouselBook(const HomeTheme::Theme& theme) const {
  bool hasCarousel = false;
  bool hasRecent = false;
  for (int slot = 0; slot < HomeTheme::slotCount(theme.layout); ++slot) {
    hasCarousel = hasCarousel || theme.widgets[slot] == HomeTheme::Widget::Carousel;
    hasRecent = hasRecent || theme.widgets[slot] == HomeTheme::Widget::Recent;
  }
  return hasCarousel && hasRecent;
}

HomeWidgetLayout::HitResult HomeWidgetLayout::hitTest(const HomeTheme::Theme& theme, const int carouselIndex,
                                                      const int favoriteIndex, const int bookCount,
                                                      const int widgetPageIndex, const int recentGridPageIndex,
                                                      const int flowIndex, const int x, const int y) const {
  const int carousel = carouselAt(theme, carouselIndex, bookCount, x, y);
  if (carousel >= 0) return {HitType::Carousel, carousel};

  const int favorite = favoritesAt(theme, favoriteIndex, x, y);
  if (favorite >= 0) return {HitType::Favorites, favorite};

  const int recent = recentAt(theme, x, y);
  if (recent >= 0) return {HitType::Recent, recent};

  const int recentGrid = recentGridAt(theme, widgetPageIndex, recentGridPageIndex, x, y);
  if (recentGrid >= 0) return {HitType::RecentGrid, recentGrid};

  if (theme.layout == HomeTheme::Layout::OneByOne) {
    int page = std::max(0, widgetPageIndex);
    for (int slot = 0; slot < HomeTheme::slotCount(theme.layout); ++slot) {
      if (theme.widgets[slot] == HomeTheme::Widget::Empty) continue;
      if (page-- != 0) continue;
      if (theme.widgets[slot] != HomeTheme::Widget::Flow &&
          theme.widgets[slot] != HomeTheme::Widget::VerticalBookshelf) break;
      const int areaY = navigation::Menu::height;
      const int areaH = renderer_.getScreenHeight() - areaY - navigation::Menu::bottomInset();
      if (theme.widgets[slot] == HomeTheme::Widget::VerticalBookshelf) {
        const int bookIndex = verticalBookshelf_.hitTest(0, areaY, renderer_.getScreenWidth(), areaH, x, y);
        if (bookIndex >= 0) return {HitType::Flow, bookIndex};
        break;
      }
      if (flow_.hitTest(x, y, 0, areaY, renderer_.getScreenWidth(), areaH)) {
        return {HitType::Flow, flowIndex};
      }
      break;
    }
  }

  const int todaysReading = todaysReadingAt(theme, x, y);
  if (todaysReading >= 0) return {HitType::TodaysReading, todaysReading};

  const int statistics = findWidget(theme, HomeTheme::Widget::Statistics, x, y);
  if (statistics >= 0) return {HitType::Statistics, statistics};

  const int heatmap = heatmapAt(theme, x, y);
  if (heatmap >= 0) return {HitType::Heatmap, heatmap};

  const int library = libraryAt(theme, x, y);
  if (library >= 0) return {HitType::Library, library};

  const int shortcut = shortcutAt(theme, x, y);
  if (shortcut >= 0) return {HitType::Shortcut, shortcut};
  return {HitType::None, -1};
}

HomeWidgetLayout::SwipeTarget HomeWidgetLayout::horizontalSwipeTarget(const HomeTheme::Theme& theme,
                                                                      const int widgetPageIndex, const int x,
                                                                      const int y) const {
  if (theme.layout == HomeTheme::Layout::Classic) {
    return x >= 0 && x < renderer_.getScreenWidth() && y >= navigation::Menu::height &&
                   y < navigation::Menu::height + Carousel::kHeight
               ? SwipeTarget::Carousel
               : SwipeTarget::None;
  }

  if (theme.layout == HomeTheme::Layout::OneByOne) {
    const int areaY = navigation::Menu::height;
    const int areaH = renderer_.getScreenHeight() - areaY - navigation::Menu::bottomInset();
    if (x < 0 || x >= renderer_.getScreenWidth() || y < areaY || y >= areaY + areaH) return SwipeTarget::None;
    int page = std::max(0, widgetPageIndex);
    for (int slot = 0; slot < HomeTheme::slotCount(theme.layout); ++slot) {
      if (theme.widgets[slot] == HomeTheme::Widget::Empty) continue;
      if (page-- != 0) continue;
      if (theme.widgets[slot] == HomeTheme::Widget::Flow ||
          theme.widgets[slot] == HomeTheme::Widget::VerticalBookshelf) {
        return SwipeTarget::Flow;
      }
      if (theme.widgets[slot] == HomeTheme::Widget::RecentGrid) return SwipeTarget::None;
      return SwipeTarget::WidgetPage;
    }
    return SwipeTarget::None;
  }

  const Grid layout = grid(theme.layout);
  for (int slot = 0; slot < HomeTheme::slotCount(theme.layout); ++slot) {
    const Bounds bounds = slotBounds(layout, slot);
    if (x < bounds.x || x >= bounds.x + bounds.width || y < bounds.y || y >= bounds.y + bounds.height) continue;
    if (theme.widgets[slot] == HomeTheme::Widget::Carousel) return SwipeTarget::Carousel;
    if (theme.widgets[slot] == HomeTheme::Widget::Favorites) return SwipeTarget::Favorites;
  }
  return SwipeTarget::None;
}

int HomeWidgetLayout::favoriteCount() const { return favorites_.count(); }

const std::string& HomeWidgetLayout::favoritePath(const int index) const { return favorites_.pathAt(index); }

int HomeWidgetLayout::recentAt(const HomeTheme::Theme& theme, const int x, const int y) const {
  if (theme.layout == HomeTheme::Layout::Classic || theme.layout == HomeTheme::Layout::OneByOne) return -1;
  const Grid layout = grid(theme.layout);
  for (int slot = 0; slot < HomeTheme::slotCount(theme.layout); ++slot) {
    if (theme.widgets[slot] != HomeTheme::Widget::Recent) continue;
    const Bounds bounds = slotBounds(layout, slot);
    const int hit = recent_.hitTest(x, y, bounds.x, bounds.y, bounds.width, bounds.height);
    if (hit >= 0) return hit;
  }
  return -1;
}

int HomeWidgetLayout::recentGridAt(const HomeTheme::Theme& theme, const int widgetPageIndex,
                                   const int recentGridPageIndex, const int x, const int y) const {
  if (theme.layout != HomeTheme::Layout::OneByOne) return -1;
  int page = std::max(0, widgetPageIndex);
  int activeSlot = -1;
  for (int slot = 0; slot < HomeTheme::slotCount(theme.layout); ++slot) {
    if (theme.widgets[slot] == HomeTheme::Widget::Empty) continue;
    if (page-- == 0) {
      activeSlot = slot;
      break;
    }
  }
  if (activeSlot < 0 || theme.widgets[activeSlot] != HomeTheme::Widget::RecentGrid) return -1;
  const int areaY = navigation::Menu::height;
  const int areaH = renderer_.getScreenHeight() - areaY - navigation::Menu::bottomInset();
  return recentGrid_.hitTest(0, areaY, renderer_.getScreenWidth(), areaH, recentGridPageIndex, x, y);
}

int HomeWidgetLayout::favoritesAt(const HomeTheme::Theme& theme, const int carouselIndex, const int x,
                                  const int y) const {
  if (theme.layout == HomeTheme::Layout::Classic || theme.layout == HomeTheme::Layout::OneByOne) return -1;
  const Grid layout = grid(theme.layout);
  for (int slot = 0; slot < HomeTheme::slotCount(theme.layout); ++slot) {
    if (theme.widgets[slot] != HomeTheme::Widget::Favorites) continue;
    const Bounds bounds = slotBounds(layout, slot);
    const int hit = favorites_.hitTest(carouselIndex, x, y, bounds.x, bounds.y, bounds.width, bounds.height,
                                       theme.carouselStyles[slot], theme.carouselLabels[slot] != 0,
                                       theme.carouselLabelColors[slot], theme.carouselFullHeight[slot] != 0,
                                       useOneByTwoWidgetLayout(theme.layout, slot));
    if (hit >= 0) return hit;
  }
  return -1;
}

int HomeWidgetLayout::todaysReadingAt(const HomeTheme::Theme& theme, const int x, const int y) const {
  if (theme.layout == HomeTheme::Layout::Classic || theme.layout == HomeTheme::Layout::OneByOne) return -1;
  const Grid layout = grid(theme.layout);
  for (int slot = 0; slot < HomeTheme::slotCount(theme.layout); ++slot) {
    if (theme.widgets[slot] != HomeTheme::Widget::TodaysReading) continue;
    const Bounds bounds = slotBounds(layout, slot);
    const bool wholeWidget = isMostCompactRow(theme.layout, slot);
    const bool hit = wholeWidget
                         ? (x >= bounds.x && x < bounds.x + bounds.width && y >= bounds.y &&
                            y < bounds.y + bounds.height)
                         : todaysReading_.buttonHitTest(x, y, bounds.x, bounds.y, bounds.width, bounds.height,
                                                        useOneByTwoWidgetLayout(theme.layout, slot),
                                                        isMostCompactRow(theme.layout, slot));
    if (hit)
      return slot;
  }
  return -1;
}

int HomeWidgetLayout::heatmapAt(const HomeTheme::Theme& theme, const int x, const int y) const {
  if (theme.layout == HomeTheme::Layout::Classic || theme.layout == HomeTheme::Layout::OneByOne) return -1;
  const Grid layout = grid(theme.layout);
  for (int slot = 0; slot < HomeTheme::slotCount(theme.layout); ++slot) {
    if (theme.widgets[slot] != HomeTheme::Widget::Heatmap) continue;
    const Bounds bounds = slotBounds(layout, slot);
    if (x >= bounds.x && x < bounds.x + bounds.width && y >= bounds.y && y < bounds.y + bounds.height) return slot;
  }
  return -1;
}

int HomeWidgetLayout::libraryAt(const HomeTheme::Theme& theme, const int x, const int y) const {
  if (theme.layout == HomeTheme::Layout::Classic || theme.layout == HomeTheme::Layout::OneByOne) return -1;
  const Grid layout = grid(theme.layout);
  for (int slot = 0; slot < HomeTheme::slotCount(theme.layout); ++slot) {
    if (theme.widgets[slot] != HomeTheme::Widget::Library) continue;
    const Bounds bounds = slotBounds(layout, slot);
    if (library_.hitTest(x, y, bounds.x, bounds.y, bounds.width, bounds.height)) return slot;
  }
  return -1;
}

const char* HomeWidgetLayout::libraryFolder(const HomeTheme::Theme& theme, const int slot) const {
  if (slot < 0 || slot >= HomeTheme::slotCount(theme.layout)) return "/";
  return theme.libraryFolders[slot][0][0] == '/' ? theme.libraryFolders[slot][0] : "/";
}

int HomeWidgetLayout::shortcutAt(const HomeTheme::Theme& theme, const int x, const int y) const {
  if (theme.layout == HomeTheme::Layout::Classic) {
    const int visible = shortcut_.hitTest(x, y, 20, navigation::Menu::height + Carousel::kHeight + 20,
                                          renderer_.getScreenWidth() - 40,
                                          renderer_.getScreenHeight() - navigation::Menu::height - Carousel::kHeight -
                                              navigation::Menu::bottomInset() - 40, false);
    return shortcut_.itemForVisibleIndex(visible);
  }
  if (theme.layout == HomeTheme::Layout::OneByOne) return -1;
  const int slot = findWidget(theme, HomeTheme::Widget::Shortcuts, x, y);
  if (slot >= 0) {
    const Bounds bounds = slotBounds(grid(theme.layout), slot);
    const int visible = shortcut_.hitTest(x, y, bounds.x, bounds.y, bounds.width, bounds.height,
                                          useOneByTwoWidgetLayout(theme.layout, slot), theme.shortcutItems);
    return shortcut_.itemForVisibleIndex(visible, theme.shortcutItems);
  }
  return -1;
}
