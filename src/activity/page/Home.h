#pragma once

/**
 * @file Home.h
 * @brief Empty home page for the new page redesign.
 */

#include "Page.h"
#include "components/home/HomeWidgetLayout.h"
#include "components/widget/ShortcutList.h"

#include <string>

struct ButtonBounds;

class Home final : public Page {
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
  bool handleShortcutDrawerInput();
  bool handleShortcut(int item);
  void drawShortcutDrawer() const;
  void advanceCarousel(int delta);
  void preloadCarousel();
  void popup() const;
  bool popupInput();
  void removeRecent();
  void deleteCache();
  ButtonBounds libraryButton() const;
  HomeWidgetLayout widgetLayout;
  ShortcutList shortcutList;
  int carouselIndex = 0;
  int favoriteIndex = 0;
  bool carouselThumbnailsPreloaded = false;
  int popupBook = -1;
  bool favoritePopupOpen = false;
  std::string popupFavoritePath;
  bool shortcutDrawerOpen = false;
};
