#pragma once

/**
 * @file Home.h
 * @brief Empty home page for the new page redesign.
 */

#include "Page.h"
#include "components/home/HomeWidgetLayout.h"
#include "state/HomeTheme.h"

#include <string>

struct ButtonBounds;

class Home final : public UiPage {
 public:
  Home(GfxRenderer& renderer, MappedInputManager& mappedInput);
  void onEnter() override;
  void loop() override;
  const char* name() const override { return "Home"; }

 protected:
  void menu() override;
  void title() const override;
  void content() override;

 private:
  bool handlePopup();
  bool handleSwipe();
  bool handleTap();
  bool handleShortcut(int item);
  void advanceCarousel(int delta);
  void advanceFlow(int delta);
  void preloadCarousel();
  void popup() const;
  bool popupInput();
  void markCompleted();
  void removeRecent();
  void deleteCache();
  ButtonBounds libraryButton() const;
  HomeWidgetLayout widgetLayout;
  int carouselIndex = 0;
  int favoriteIndex = 0;
  int widgetPageIndex = 0;
  int recentGridPageIndex = 0;
  int flowIndex = 0;
  bool carouselThumbnailsPreloaded = false;
  int popupBook = -1;
  bool favoritePopupOpen = false;
  bool heatmapPopupOpen = false;
  HomeTheme::HeatmapView heatmapPopupView = HomeTheme::HeatmapView::Weekly;
  std::string popupFavoritePath;
};
