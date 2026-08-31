#pragma once

#include <cstdint>

namespace HomeTheme {

enum class Layout : uint8_t {
  Classic = 0,
  OneByTwo = 1,
  TwoByTwo = 2,
};

enum class Widget : uint8_t {
  Empty = 0,
  Carousel = 1,
  Shortcuts = 2,
  Clock = 3,
  Recent = 4,
  Temperature = 5,
  Calendar = 6,
  ListShortcuts = 7,
  Humidity = 8,
  TodaysReading = 9,
  Favorites = 10,
  Heatmap = 11,
};

enum class HeatmapView : uint8_t {
  Daily = 0,
  Weekly = 1,
  Monthly = 2,
};

enum class CarouselStyle : uint8_t {
  Centered = 0,
  Left = 1,
  Right = 2,
};

enum class CarouselLabelColor : uint8_t {
  Black = 0,
  Gray = 1,
};

enum class CarouselShadowStyle : uint8_t {
  None = 0,
  Black = 1,
  Gray = 2,
};

enum class Border : uint8_t {
  None = 0,
  Subtle = 1,
  Normal = 2,
  Thick = 3,
};

struct Theme {
  char name[32];
  Layout layout;
  Widget widgets[4];
  Border borders[4];
  uint8_t backgrounds[4];
  CarouselStyle carouselStyles[4];
  uint8_t carouselLabels[4];
  CarouselLabelColor carouselLabelColors[4];
  CarouselShadowStyle carouselShadowStyles[4];
  HeatmapView heatmapViews[4];
};

void load();
bool save();

int count();
const Theme& at(int index);
const Theme& sleep();
int activeIndex();
const Theme& active();
void activate(int index);
int add(Layout layout, const Widget* widgets, const Border* borders, const uint8_t* backgrounds,
        const CarouselStyle* carouselStyles, const uint8_t* carouselLabels, const CarouselLabelColor* carouselLabelColors,
        const CarouselShadowStyle* carouselShadowStyles, const HeatmapView* heatmapViews,
        int slotCount);
void update(int index, Layout layout, const Widget* widgets, const Border* borders, const uint8_t* backgrounds,
            const CarouselStyle* carouselStyles, const uint8_t* carouselLabels,
            const CarouselLabelColor* carouselLabelColors, const CarouselShadowStyle* carouselShadowStyles,
            const HeatmapView* heatmapViews, int slotCount);
void updateSleep(Layout layout, const Widget* widgets, const Border* borders, const uint8_t* backgrounds,
                 const CarouselStyle* carouselStyles, const uint8_t* carouselLabels,
                 const CarouselLabelColor* carouselLabelColors, const CarouselShadowStyle* carouselShadowStyles,
                 const HeatmapView* heatmapViews, int slotCount);
bool remove(int index);

const char* layoutLabel(Layout layout);
const char* widgetLabel(Widget widget);
const char* carouselStyleLabel(CarouselStyle style);
const char* carouselLabelColorLabel(CarouselLabelColor color);
const char* carouselShadowStyleLabel(CarouselShadowStyle style);
CarouselStyle defaultCarouselStyle(Widget widget);
const char* heatmapViewLabel(HeatmapView view);
int slotCount(Layout layout);

}  // namespace HomeTheme
