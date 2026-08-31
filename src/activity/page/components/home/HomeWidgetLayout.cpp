#include "HomeWidgetLayout.h"

#include <GfxRenderer.h>

#include "../../navigation/Menu.h"

HomeWidgetLayout::HomeWidgetLayout(GfxRenderer& renderer)
    : renderer_(renderer), carousel_(renderer), clock_(renderer), calendar_(renderer), recent_(renderer),
      shortcut_(renderer), shortcutList_(renderer), temperature_(renderer), humidity_(renderer), todaysReading_(renderer),
      favorites_(renderer), heatmap_(renderer) {}

void HomeWidgetLayout::render(const HomeTheme::Theme& theme, const int carouselIndex, const int favoriteIndex) const {
  switch (theme.layout) {
    case HomeTheme::Layout::Classic:
      renderClassic(carouselIndex);
      return;
    case HomeTheme::Layout::OneByTwo:
    case HomeTheme::Layout::TwoByTwo:
      renderGrid(theme, carouselIndex, favoriteIndex);
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
  for (int slot = 0; slot < HomeTheme::slotCount(theme.layout); ++slot) {
    if (theme.widgets[slot] != HomeTheme::Widget::Carousel) continue;
    const Bounds bounds = slotBounds(layout, slot);
    carousel_.preload(carouselIndex, bounds.x, bounds.y, bounds.width, bounds.height, theme.carouselStyles[slot],
                       theme.carouselLabels[slot] != 0, theme.carouselLabelColors[slot]);
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
    if (theme.widgets[slot] == HomeTheme::Widget::Heatmap &&
        heatmap_.needsRefresh(theme.heatmapViews[slot])) return true;
  }
  return false;
}

void HomeWidgetLayout::renderSleep(const HomeTheme::Theme& theme) const {
  if (theme.layout == HomeTheme::Layout::Classic) return;
  renderGrid(theme, 0, 0, true);
}

HomeWidgetLayout::Grid HomeWidgetLayout::grid(const HomeTheme::Layout layout, const bool sleep,
                                              const HomeTheme::Theme* theme) const {
  constexpr int gap = 0;
  const int baseColumns = layout == HomeTheme::Layout::TwoByTwo ? 2 : 1;
  constexpr int baseRows = 2;
  // The widget layout is designed for the same content band as Home: below
  // the header and above the bottom navigation area. Sleep does not draw the
  // menus itself, but its widgets still need to use that geometry so their
  // visual center matches the layout they were built for.
  const int areaY = navigation::Menu::height;
  const int fullWidth = renderer_.getScreenWidth();
  const int fullHeight = renderer_.getScreenHeight() - areaY - navigation::Menu::bottomHeight;

  int columns = baseColumns;
  int rows = baseRows;
  int slotColumnOffset = 0;
  int slotRowOffset = 0;
  int centeredX = 0;
  int centeredY = areaY;
  int centeredWidth = fullWidth;
  int centeredHeight = fullHeight;

  // Sleep screens should center the configured widget group. A single widget
  // in the default first slot must not remain in the upper half of the panel;
  // when several widgets are present, keep their grid relationship intact.
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
      centeredX = (fullWidth - centeredWidth) / 2;
      centeredY = areaY + (fullHeight - centeredHeight) / 2;
    }
  }

  return {columns, rows, centeredX, centeredY, centeredWidth, centeredHeight, gap, baseColumns, slotColumnOffset,
          slotRowOffset};
}

HomeWidgetLayout::Bounds HomeWidgetLayout::slotBounds(const Grid& layout, const int slot) const {
  const int cellWidth = (layout.areaW - layout.gap * (layout.columns - 1)) / layout.columns;
  const int cellHeight = (layout.areaH - layout.gap * (layout.rows - 1)) / layout.rows;
  const int column = slot % layout.slotColumns - layout.slotColumnOffset;
  const int row = slot / layout.slotColumns - layout.slotRowOffset;
  return {layout.areaX + column * (cellWidth + layout.gap), layout.areaY + row * (cellHeight + layout.gap), cellWidth,
          cellHeight};
}

void HomeWidgetLayout::renderClassic(const int carouselIndex) const {
  carousel_.render(carouselIndex, 0, navigation::Menu::height, renderer_.getScreenWidth(), Carousel::kHeight);
  shortcut_.render(20, navigation::Menu::height + Carousel::kHeight + 20, renderer_.getScreenWidth() - 40,
                   renderer_.getScreenHeight() - navigation::Menu::height - Carousel::kHeight -
                       navigation::Menu::bottomHeight - 40);
}

void HomeWidgetLayout::renderGrid(const HomeTheme::Theme& theme, const int carouselIndex, const int favoriteIndex,
                                  const bool sleep) const {
  const Grid layout = grid(theme.layout, sleep, sleep ? &theme : nullptr);
  for (int slot = 0; slot < HomeTheme::slotCount(theme.layout); ++slot) {
    const Bounds bounds = slotBounds(layout, slot);
    switch (theme.widgets[slot]) {
      case HomeTheme::Widget::Carousel:
        carousel_.render(carouselIndex, bounds.x, bounds.y, bounds.width, bounds.height,
                         theme.backgrounds[slot] != 0, theme.carouselStyles[slot], theme.carouselLabels[slot] != 0,
                         theme.carouselLabelColors[slot], theme.carouselShadowStyles[slot]);
        break;
      case HomeTheme::Widget::Shortcuts:
        shortcut_.render(bounds.x, bounds.y, bounds.width, bounds.height);
        break;
      case HomeTheme::Widget::ListShortcuts:
        shortcutList_.render(bounds.x, bounds.y, bounds.width, bounds.height);
        break;
      case HomeTheme::Widget::Clock:
        clock_.render(bounds.x, bounds.y, bounds.width, bounds.height);
        break;
      case HomeTheme::Widget::Calendar:
        calendar_.render(bounds.x, bounds.y, bounds.width, bounds.height);
        break;
      case HomeTheme::Widget::Recent:
        recent_.render(bounds.x, bounds.y, bounds.width, bounds.height, theme.backgrounds[slot] != 0,
                       theme.carouselStyles[slot], theme.carouselLabels[slot] != 0, theme.carouselLabelColors[slot],
                       theme.carouselShadowStyles[slot]);
        break;
#if FREEINK_DEVICE_STICKY
      case HomeTheme::Widget::Temperature:
        temperature_.render(bounds.x, bounds.y, bounds.width, bounds.height);
        break;
      case HomeTheme::Widget::Humidity:
        humidity_.render(bounds.x, bounds.y, bounds.width, bounds.height);
        break;
#endif
      case HomeTheme::Widget::TodaysReading:
        todaysReading_.render(bounds.x, bounds.y, bounds.width, bounds.height);
        break;
      case HomeTheme::Widget::Favorites:
        favorites_.render(favoriteIndex, bounds.x, bounds.y, bounds.width, bounds.height,
                          theme.backgrounds[slot] != 0, theme.carouselStyles[slot], theme.carouselLabels[slot] != 0,
                          theme.carouselLabelColors[slot], theme.carouselShadowStyles[slot]);
        break;
      case HomeTheme::Widget::Heatmap:
        heatmap_.render(bounds.x, bounds.y, bounds.width, bounds.height, theme.heatmapViews[slot]);
        break;
      case HomeTheme::Widget::Empty:
      default:
        break;
    }
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
      if (layout.columns == 2) renderer_.line.render(dividerX, layout.areaY, dividerX, layout.areaY + layout.areaH, true);
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
  if (theme.layout == HomeTheme::Layout::Classic) {
    return carousel_.hitTest(carouselIndex, count, x, y, 0, navigation::Menu::height, renderer_.getScreenWidth(),
                             Carousel::kHeight, HomeTheme::CarouselStyle::Centered, true);
  }
  const int slot = findWidget(theme, HomeTheme::Widget::Carousel, x, y);
  if (slot < 0) return -1;
  const Bounds bounds = slotBounds(grid(theme.layout), slot);
  return carousel_.hitTest(carouselIndex, count, x, y, bounds.x, bounds.y, bounds.width, bounds.height,
                           theme.carouselStyles[slot], theme.carouselLabels[slot] != 0,
                           theme.carouselLabelColors[slot]);
}

HomeWidgetLayout::HitResult HomeWidgetLayout::hitTest(const HomeTheme::Theme& theme, const int carouselIndex,
                                                      const int favoriteIndex, const int bookCount, const int x,
                                                      const int y) const {
  const int carousel = carouselAt(theme, carouselIndex, bookCount, x, y);
  if (carousel >= 0) return {HitType::Carousel, carousel};

  const int favorite = favoritesAt(theme, favoriteIndex, x, y);
  if (favorite >= 0) return {HitType::Favorites, favorite};

  const int recent = recentAt(theme, x, y);
  if (recent >= 0) return {HitType::Recent, recent};

  const int todaysReading = todaysReadingAt(theme, x, y);
  if (todaysReading >= 0) return {HitType::TodaysReading, todaysReading};

  const int shortcut = shortcutAt(theme, x, y);
  if (shortcut >= 0) return {HitType::Shortcut, shortcut};
  return {HitType::None, -1};
}

HomeWidgetLayout::SwipeTarget HomeWidgetLayout::horizontalSwipeTarget(const HomeTheme::Theme& theme, const int x,
                                                                       const int y) const {
  if (theme.layout == HomeTheme::Layout::Classic) {
    return x >= 0 && x < renderer_.getScreenWidth() && y >= navigation::Menu::height &&
                   y < navigation::Menu::height + Carousel::kHeight
               ? SwipeTarget::Carousel
               : SwipeTarget::None;
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
  if (theme.layout == HomeTheme::Layout::Classic) return -1;
  const Grid layout = grid(theme.layout);
  for (int slot = 0; slot < HomeTheme::slotCount(theme.layout); ++slot) {
    if (theme.widgets[slot] != HomeTheme::Widget::Recent) continue;
    const Bounds bounds = slotBounds(layout, slot);
    const int hit = recent_.hitTest(x, y, bounds.x, bounds.y, bounds.width, bounds.height);
    if (hit >= 0) return hit;
  }
  return -1;
}

int HomeWidgetLayout::favoritesAt(const HomeTheme::Theme& theme, const int carouselIndex, const int x,
                                  const int y) const {
  if (theme.layout == HomeTheme::Layout::Classic) return -1;
  const Grid layout = grid(theme.layout);
  for (int slot = 0; slot < HomeTheme::slotCount(theme.layout); ++slot) {
    if (theme.widgets[slot] != HomeTheme::Widget::Favorites) continue;
    const Bounds bounds = slotBounds(layout, slot);
    const int hit = favorites_.hitTest(carouselIndex, x, y, bounds.x, bounds.y, bounds.width, bounds.height,
                                       theme.carouselStyles[slot], theme.carouselLabels[slot] != 0,
                                       theme.carouselLabelColors[slot]);
    if (hit >= 0) return hit;
  }
  return -1;
}

int HomeWidgetLayout::todaysReadingAt(const HomeTheme::Theme& theme, const int x, const int y) const {
  if (theme.layout == HomeTheme::Layout::Classic) return -1;
  const Grid layout = grid(theme.layout);
  for (int slot = 0; slot < HomeTheme::slotCount(theme.layout); ++slot) {
    if (theme.widgets[slot] != HomeTheme::Widget::TodaysReading) continue;
    const Bounds bounds = slotBounds(layout, slot);
    if (todaysReading_.buttonHitTest(x, y, bounds.x, bounds.y, bounds.width, bounds.height)) return slot;
  }
  return -1;
}

int HomeWidgetLayout::shortcutAt(const HomeTheme::Theme& theme, const int x, const int y) const {
  if (theme.layout == HomeTheme::Layout::Classic) {
    return shortcut_.hitTest(x, y, 20, navigation::Menu::height + Carousel::kHeight + 20,
                             renderer_.getScreenWidth() - 40,
                             renderer_.getScreenHeight() - navigation::Menu::height - Carousel::kHeight -
                                 navigation::Menu::bottomHeight - 40);
  }
  const int slot = findWidget(theme, HomeTheme::Widget::Shortcuts, x, y);
  if (slot >= 0) {
    const Bounds bounds = slotBounds(grid(theme.layout), slot);
    return shortcut_.hitTest(x, y, bounds.x, bounds.y, bounds.width, bounds.height);
  }
  const int listSlot = findWidget(theme, HomeTheme::Widget::ListShortcuts, x, y);
  if (listSlot < 0) return -1;
  const Bounds bounds = slotBounds(grid(theme.layout), listSlot);
  return shortcutList_.hitTest(x, y, bounds.x, bounds.y, bounds.width, bounds.height);
}
