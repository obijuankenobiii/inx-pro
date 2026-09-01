#pragma once

#include <functional>

#include "activity/ActivityWithSubactivity.h"
#include "activity/page/components/widget/Calendar.h"
#include "activity/page/components/widget/Carousel.h"
#include "activity/page/components/widget/Clock.h"
#include "activity/page/components/widget/Recent.h"
#include "activity/page/components/widget/Shortcut.h"
#include "activity/page/components/widget/ShortcutList.h"
#include "activity/page/components/widget/Temperature.h"
#include "activity/page/components/widget/Humidity.h"
#include "activity/page/components/widget/TodaysReading.h"
#include "activity/page/components/widget/Favorites.h"
#include "activity/page/components/widget/Heatmap.h"
#include "state/HomeTheme.h"

class ThemePickerActivity final : public ActivityWithSubactivity {
 public:
  ThemePickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::function<void()> onBack,
                      bool openSleepTheme = false)
      : ActivityWithSubactivity("ThemePicker", renderer, mappedInput), carousel_(renderer), shortcut_(renderer),
        shortcutList_(renderer), clock_(renderer), calendar_(renderer), recent_(renderer), temperature_(renderer),
        humidity_(renderer), todaysReading_(renderer),
        favorites_(renderer), heatmap_(renderer),
        onBack_(std::move(onBack)), openSleepTheme_(openSleepTheme) {}

  void onEnter() override;
  void loop() override;

 private:
  enum class Screen { List, Layout, Widgets };

  Screen screen_ = Screen::List;
  int selected_ = 0;
  HomeTheme::Layout layout_ = HomeTheme::Layout::OneByTwo;
  int widgetSlot_ = 0;
  HomeTheme::Widget widgets_[4] = {};
  HomeTheme::Border borders_[4] = {};
  uint8_t backgrounds_[4] = {};
  HomeTheme::CarouselStyle carouselStyles_[4] = {};
  uint8_t carouselLabels_[4] = {};
  HomeTheme::CarouselLabelColor carouselLabelColors_[4] = {};
  HomeTheme::CarouselShadowStyle carouselShadowStyles_[4] = {};
  HomeTheme::HeatmapView heatmapViews_[4] = {};
  bool editingExisting_ = false;
  bool editingSleep_ = false;
  bool openSleepTheme_ = false;
  bool widgetPopup_ = false;
  bool borderPopup_ = false;
  bool carouselSettingsFinished_ = false;
  bool heatmapSettingsFinished_ = false;
  bool temperatureSettingsFinished_ = false;
  int popupSelected_ = 0;
  int widgetPopupScroll_ = 0;
  int borderPopupSelected_ = 0;
  Carousel carousel_;
  Shortcut shortcut_;
  ShortcutList shortcutList_;
  Clock clock_;
  Calendar calendar_;
  Recent recent_;
  Temperature temperature_;
  Humidity humidity_;
  TodaysReading todaysReading_;
  Favorites favorites_;
  Heatmap heatmap_;
  std::function<void()> onBack_;

  void render();
  void renderList();
  void renderLayoutPicker();
  void renderWidgetPicker();
  void renderWidgetPreview(HomeTheme::Widget widget, int x, int y, int width, int height, bool background,
                           HomeTheme::CarouselStyle style, bool showLabel, HomeTheme::CarouselLabelColor labelColor,
                           HomeTheme::CarouselShadowStyle shadowStyle, HomeTheme::HeatmapView heatmapView);
  void renderBorder(HomeTheme::Border border, int x, int y, int width, int height);
  void handleTouch(int x, int y);
  void editTheme();
  void saveEditorAndClose();
  void openWidgetPopup(int slot);
  void renderWidgetPopup();
  void openBorderPopup(int slot);
  void openCarouselSettings(int slot);
  void openHeatmapSettings(int slot);
  void openTemperatureSettings(int slot);
  void renderBorderPopup();
  void moveWidgetPopupSelection(int delta);
  void pageWidgetPopup(int delta);
  void close();
};
