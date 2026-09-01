#pragma once

#include "../widget/Carousel.h"
#include "../widget/Clock.h"
#include "../widget/Calendar.h"
#include "../widget/Recent.h"
#include "../widget/Shortcut.h"
#include "../widget/ShortcutList.h"
#include "../widget/Temperature.h"
#include "../widget/Humidity.h"
#include "../widget/TodaysReading.h"
#include "../widget/Favorites.h"
#include "../widget/Heatmap.h"
#include "state/HomeTheme.h"

class GfxRenderer;

class HomeWidgetLayout final {
 public:
  enum class HitType { None, Carousel, Recent, Favorites, TodaysReading, Heatmap, Shortcut };
  enum class SwipeTarget { None, Carousel, Favorites };

  struct HitResult {
    HitType type;
    int index;
  };

  explicit HomeWidgetLayout(GfxRenderer& renderer);

  void render(const HomeTheme::Theme& theme, int carouselIndex, int favoriteIndex) const;
  void preloadCarousel(const HomeTheme::Theme& theme, int carouselIndex) const;
  void invalidateFavorites() const;
  int favoriteCount() const;
  const std::string& favoritePath(int index) const;
  void renderSleep(const HomeTheme::Theme& theme) const;
  bool needsRefresh(const HomeTheme::Theme& theme) const;
  HitResult hitTest(const HomeTheme::Theme& theme, int carouselIndex, int favoriteIndex, int bookCount, int x,
                    int y) const;
  SwipeTarget horizontalSwipeTarget(const HomeTheme::Theme& theme, int x, int y) const;

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
  };

  Grid grid(const HomeTheme::Layout layout, bool sleep = false, const HomeTheme::Theme* theme = nullptr) const;
  Bounds slotBounds(const Grid& grid, int slot) const;
  void renderClassic(int carouselIndex) const;
  void renderGrid(const HomeTheme::Theme& theme, int carouselIndex, int favoriteIndex, bool sleep = false) const;
  void renderBorder(HomeTheme::Border border, const Grid& layout) const;
  int findWidget(const HomeTheme::Theme& theme, HomeTheme::Widget widget, int x, int y) const;
  int carouselAt(const HomeTheme::Theme& theme, int carouselIndex, int count, int x, int y) const;
  int recentAt(const HomeTheme::Theme& theme, int x, int y) const;
  int favoritesAt(const HomeTheme::Theme& theme, int carouselIndex, int x, int y) const;
  int todaysReadingAt(const HomeTheme::Theme& theme, int x, int y) const;
  int heatmapAt(const HomeTheme::Theme& theme, int x, int y) const;
  int shortcutAt(const HomeTheme::Theme& theme, int x, int y) const;

  GfxRenderer& renderer_;
  Carousel carousel_;
  Clock clock_;
  Calendar calendar_;
  Recent recent_;
  Shortcut shortcut_;
  ShortcutList shortcutList_;
  Temperature temperature_;
  Humidity humidity_;
  TodaysReading todaysReading_;
  Favorites favorites_;
  Heatmap heatmap_;
};
