#pragma once

#include "../widget/Calendar.h"
#include "../widget/Carousel.h"
#include "../widget/Clock.h"
#include "../widget/Description.h"
#include "../widget/Favorites.h"
#include "../widget/Heatmap.h"
#include "../widget/Humidity.h"
#include "../widget/Library.h"
#include "../widget/Recent.h"
#include "../widget/RecentGrid.h"
#include "../widget/Flow.h"
#include "../widget/verticalbookshelf/VerticalBookshelf.h"
#include "../widget/Shortcut.h"
#include "../widget/ShortcutList.h"
#include "../widget/Temperature.h"
#include "../widget/TodaysReading.h"
#include "../widget/WeeklyStatistics.h"
#include "../widget/Streaks.h"
#include "state/HomeTheme.h"

class GfxRenderer;

class HomeWidgetLayout final {
 public:
  enum class HitType {
    None,
    Carousel,
    Recent,
    RecentGrid,
    Flow,
    Favorites,
    TodaysReading,
    Statistics,
    Heatmap,
    Library,
    Shortcut
  };
  enum class SwipeTarget { None, Carousel, Favorites, Flow, WidgetPage };

  struct HitResult {
    HitType type;
    int index;
  };

  explicit HomeWidgetLayout(GfxRenderer& renderer);

  void render(const HomeTheme::Theme& theme, int carouselIndex, int favoriteIndex, int widgetPageIndex,
              int recentGridPageIndex, int flowIndex) const;
  void preloadCarousel(const HomeTheme::Theme& theme, int carouselIndex) const;
  void invalidateFavorites() const;
  int favoriteCount() const;
  const std::string& favoritePath(int index) const;
  const char* libraryFolder(const HomeTheme::Theme& theme, int slot) const;
  void renderSleep(const HomeTheme::Theme& theme) const;
  bool needsRefresh(const HomeTheme::Theme& theme) const;
  int carouselBookCount(const HomeTheme::Theme& theme, int bookCount) const;
  HitResult hitTest(const HomeTheme::Theme& theme, int carouselIndex, int favoriteIndex, int bookCount,
                    int widgetPageIndex, int recentGridPageIndex, int flowIndex, int x, int y) const;
  SwipeTarget horizontalSwipeTarget(const HomeTheme::Theme& theme, int widgetPageIndex, int x, int y) const;
  int oneByOnePageCount(const HomeTheme::Theme& theme) const;
  int recentGridPageCount(const HomeTheme::Theme& theme, int widgetPageIndex, int bookCount) const;

 private:
  struct Bounds {
    int x;
    int y;
    int width;
    int height;
  };

  struct Grid {
    int columns;
    int rows;
    int areaX;
    int areaY;
    int areaW;
    int areaH;
    int gap;
    int slotColumns;
    int slotColumnOffset;
    int slotRowOffset;
    HomeTheme::Layout layout;
  };

  Grid grid(const HomeTheme::Layout layout, bool sleep = false, const HomeTheme::Theme* theme = nullptr) const;
  Bounds slotBounds(const Grid& grid, int slot) const;
  bool hideFirstCarouselBook(const HomeTheme::Theme& theme) const;
  void renderClassic(int carouselIndex) const;
  void renderGrid(const HomeTheme::Theme& theme, int carouselIndex, int favoriteIndex, bool sleep = false) const;
  void renderOneByOne(const HomeTheme::Theme& theme, int carouselIndex, int favoriteIndex, int widgetPageIndex,
                      int recentGridPageIndex, int flowIndex) const;
  void renderSlot(const HomeTheme::Theme& theme, int slot, const Bounds& bounds, int carouselIndex,
                  int favoriteIndex, int descriptionRecentIndex, bool hideFirst, int recentGridPageIndex,
                  int flowIndex) const;
  void renderBorders(const HomeTheme::Theme& theme, const Grid& layout) const;
  void renderBorder(HomeTheme::Border border, const Grid& layout) const;
  int findWidget(const HomeTheme::Theme& theme, HomeTheme::Widget widget, int x, int y) const;
  int carouselAt(const HomeTheme::Theme& theme, int carouselIndex, int count, int x, int y) const;
  int recentAt(const HomeTheme::Theme& theme, int x, int y) const;
  int recentGridAt(const HomeTheme::Theme& theme, int widgetPageIndex, int recentGridPageIndex, int x, int y) const;
  int favoritesAt(const HomeTheme::Theme& theme, int carouselIndex, int x, int y) const;
  int todaysReadingAt(const HomeTheme::Theme& theme, int x, int y) const;
  int heatmapAt(const HomeTheme::Theme& theme, int x, int y) const;
  int libraryAt(const HomeTheme::Theme& theme, int x, int y) const;
  int shortcutAt(const HomeTheme::Theme& theme, int x, int y) const;

  GfxRenderer& renderer_;
  Carousel carousel_;
  Clock clock_;
  Calendar calendar_;
  Recent recent_;
  RecentGrid recentGrid_;
  Flow flow_;
  widget::verticalbookshelf::VerticalBookshelf verticalBookshelf_;
  Shortcut shortcut_;
  ShortcutList shortcutList_;
  Temperature temperature_;
  Humidity humidity_;
  TodaysReading todaysReading_;
  WeeklyStatistics weeklyStatistics_;
  Streaks streaks_;
  Favorites favorites_;
  Heatmap heatmap_;
  LibraryWidget library_;
  Description description_;
};
