/**
 * @file Home.cpp
 * @brief Empty home page for the new page redesign.
 */

#include "Home.h"

#include "HomeSubPage.h"

#include <GfxRenderer.h>
#include <SDCardManager.h>

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "components/global/Button.h"
#include "components/global/PopUp.h"
#include "components/global/Sidebar.h"
#include "images/Hamburger.h"
#include "state/BookState.h"
#include "state/RecentBooks.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"

extern void openReaderFromCallback(const std::string& path, std::function<void()> returnToCaller);
extern void onGoToHome();
extern void onGoToLibrary(const std::string& path);
extern void openHomeSubPage(HomeSubPage::Section section);
extern void onGoToStatistics();
extern void onGoToHeatmapReport(HomeTheme::HeatmapView view);

namespace {

constexpr unsigned long longPressMs = 500;

std::string cachePath(const RecentBook& book) {
  if (!book.cachePath.empty()) return book.cachePath;
  return "/.metadata/epub/" + std::to_string(std::hash<std::string>{}(book.path));
}

bool removeTree(const std::string& path, int& removed) {
  FsFile directory = SdMan.open(path.c_str());
  if (!directory || !directory.isDirectory()) return false;

  char name[128] = {};
  while (true) {
    FsFile entry = directory.openNextFile();
    if (!entry) break;
    const bool isDirectory = entry.isDirectory();
    entry.getName(name, sizeof(name));
    entry.close();

    const std::string child = path + "/" + name;
    if (isDirectory ? !removeTree(child, removed) : !SdMan.remove(child.c_str())) {
      directory.close();
      return false;
    }
    if ((++removed & 7) == 0) {
      esp_task_wdt_reset();
      vTaskDelay(pdMS_TO_TICKS(1));
    }
  }
  directory.close();
  return SdMan.removeDir(path.c_str());
}

}  // namespace

Home::Home(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Page("Home", renderer, mappedInput), widgetLayout(renderer), shortcutList(renderer) {}

void Home::onEnter() {
  Page::onEnter();
  // X4 Pro keeps separate active/write buffers. Rebase the Home render from the frame
  // currently on the panel before composing the new page to avoid Library residue.
  renderer.syncWriteBufferFromActive();
  carouselIndex = 0;
  favoriteIndex = 0;
  widgetLayout.invalidateFavorites();
  carouselThumbnailsPreloaded = false;
  popupBook = -1;
  favoritePopupOpen = false;
  heatmapPopupOpen = false;
  popupFavoritePath.clear();
  shortcutDrawerOpen = false;
}

void Home::menu() {
  Page::menu();
#if FREEINK_DEVICE_X4PRO
  if (!isLightDrawerOpen()) drawBattery(renderer);
#else
  drawBattery(renderer);
#endif
  if (shortcutDrawerOpen) drawShortcutDrawer();
}

void Home::title() const {
  renderer.bitmap.icon(Hamburger, navigation::Menu::leftMargin, navigation::Menu::topPadding,
                       navigation::Menu::iconSize, navigation::Menu::iconSize);
  const int font = MONTSERRAT_16_FONT_ID;
  const int textY = navigation::Menu::topPadding +
                    (navigation::Menu::iconSize - renderer.text.getLineHeight(font)) / 2;
  renderer.text.render(font, navigation::Menu::leftMargin + navigation::Menu::iconSize + 12, textY, "Home", true,
                       EpdFontFamily::BOLD);
}

ButtonBounds Home::libraryButton() const {
  const int width = Button::width(renderer, "Library", MONTSERRAT_10_FONT_ID);
  const int x = (renderer.getScreenWidth() - width) / 2;
  const int y = navigation::Menu::height + Carousel::kHeight / 2 + 14;
  return {x, y, width, Button::height};
}

void Home::loop() {
  if (shortcutDrawerOpen && handleShortcutDrawerInput()) return;
  if ((popupBook >= 0 || favoritePopupOpen || heatmapPopupOpen) && handlePopup()) return;
  if (menuInput()) return;
  if (isOpen()) {
    renderPage();
    return;
  }

  if (widgetLayout.needsRefresh(HomeTheme::active())) updateRequired = true;
  if (handleSwipe()) return;
  if (handleTap()) return;
  renderPage();
  preloadCarousel();
}

void Home::content() {
  widgetLayout.render(HomeTheme::active(), carouselIndex, favoriteIndex);
  if (popupBook >= 0 || favoritePopupOpen || heatmapPopupOpen) popup();
}

bool Home::handlePopup() {
  if (heatmapPopupOpen) {
    if (popupInput()) return true;
    renderPage();
    return true;
  }
  if (favoritePopupOpen) {
    BookState::Book book;
    if (popupFavoritePath.empty() || !BOOK_STATE.findBook(popupFavoritePath, book) || !book.isFavorite) {
      favoritePopupOpen = false;
      popupFavoritePath.clear();
      updateRequired = true;
      renderPage();
      return true;
    }
    if (popupInput()) return true;
    renderPage();
    return true;
  }
  if (popupBook >= RECENT_BOOKS.getCount()) {
    popupBook = -1;
    updateRequired = true;
    renderPage();
    return true;
  }
  if (popupInput()) return true;
  renderPage();
  return true;
}

bool Home::handleSwipe() {
  if (mappedInput.wasTouchSwipeUp() || mappedInput.wasTouchSwipeDown()) return false;
  float swipeNx = 0.0f;
  float swipeNy = 0.0f;
  const bool swipeLeft = mappedInput.wasTouchSwipeLeftInScreen(renderer, swipeNx, swipeNy);
  const bool swipeRight = !swipeLeft && mappedInput.wasTouchSwipeRightInScreen(renderer, swipeNx, swipeNy);
  if (!swipeLeft && !swipeRight) return false;

  if (swipeRight && static_cast<int>(swipeNy * renderer.getScreenHeight()) < navigation::Menu::height) {
    shortcutDrawerOpen = true;
    updateRequired = true;
    renderPage();
    return true;
  }

  const int swipeX = static_cast<int>(swipeNx * renderer.getScreenWidth());
  const int swipeY = static_cast<int>(swipeNy * renderer.getScreenHeight());
  const HomeWidgetLayout::SwipeTarget target =
      widgetLayout.horizontalSwipeTarget(HomeTheme::active(), swipeX, swipeY);
  if (target == HomeWidgetLayout::SwipeTarget::Carousel) {
    advanceCarousel(swipeLeft ? 1 : -1);
    return true;
  }
  if (target == HomeWidgetLayout::SwipeTarget::Favorites) {
    const int count = widgetLayout.favoriteCount();
    if (count <= 0) return true;
    favoriteIndex = (favoriteIndex + count + (swipeLeft ? 1 : -1)) % count;
    updateRequired = true;
    renderPage();
    return true;
  }
  return false;
}

void Home::advanceCarousel(const int delta) {
  const int bookCount = RECENT_BOOKS.getCount();
  if (bookCount <= 0) {
    carouselIndex = 0;
    return;
  }
  carouselIndex = (carouselIndex + bookCount + delta) % bookCount;
  updateRequired = true;
  renderPage();
}

void Home::preloadCarousel() {
  if (carouselThumbnailsPreloaded) return;
  widgetLayout.preloadCarousel(HomeTheme::active(), carouselIndex);
  carouselThumbnailsPreloaded = true;
}

bool Home::handleTap() {
  if (!mappedInput.hasTouch()) return false;
  float tapNx = 0.0f;
  float tapNy = 0.0f;
  if (!mappedInput.wasTouchTapInScreen(renderer, tapNx, tapNy)) return false;
  const int tapX = static_cast<int>(tapNx * renderer.getScreenWidth());
  const int tapY = static_cast<int>(tapNy * renderer.getScreenHeight());
  const int hamburgerX = navigation::Menu::leftMargin - 10;
  const int hamburgerY = navigation::Menu::topPadding - 10;
  const int hamburgerSize = navigation::Menu::iconSize;
  if (tapX >= hamburgerX && tapX < hamburgerX + hamburgerSize && tapY >= hamburgerY &&
      tapY < hamburgerY + hamburgerSize) {
    shortcutDrawerOpen = true;
    updateRequired = true;
    renderPage();
    return true;
  }
  const int bookCount = RECENT_BOOKS.getCount();
  const HomeWidgetLayout::HitResult favoriteHit =
      widgetLayout.hitTest(HomeTheme::active(), carouselIndex, favoriteIndex, bookCount, tapX, tapY);
  if (favoriteHit.type == HomeWidgetLayout::HitType::Heatmap) {
    if (mappedInput.lastTouchHeldMs() >= longPressMs) {
      heatmapPopupOpen = true;
      heatmapPopupView = HomeTheme::active().heatmapViews[favoriteHit.index];
      updateRequired = true;
    }
    return true;
  }
  if (favoriteHit.type == HomeWidgetLayout::HitType::Favorites) {
    const std::string& path = widgetLayout.favoritePath(favoriteHit.index);
    if (mappedInput.lastTouchHeldMs() >= longPressMs) {
      favoritePopupOpen = !path.empty();
      popupFavoritePath = path;
      popupBook = -1;
      updateRequired = true;
      return true;
    }
    if (!path.empty()) openReaderFromCallback(path, [] { onGoToHome(); });
    return true;
  }
  if (bookCount > 0) {
    const HomeWidgetLayout::HitResult hit =
        widgetLayout.hitTest(HomeTheme::active(), carouselIndex, favoriteIndex, bookCount, tapX, tapY);
    if (hit.type == HomeWidgetLayout::HitType::Shortcut) return handleShortcut(hit.index);
    if (hit.type == HomeWidgetLayout::HitType::Recent) {
      if (mappedInput.lastTouchHeldMs() >= longPressMs) {
        popupBook = hit.index;
        updateRequired = true;
        return true;
      }
      openReaderFromCallback(RECENT_BOOKS.getBooks()[static_cast<size_t>(hit.index)].path, [] { onGoToHome(); });
      return true;
    }
    if (hit.type == HomeWidgetLayout::HitType::TodaysReading) {
      openReaderFromCallback(RECENT_BOOKS.getBooks().front().path, [] { onGoToHome(); });
      return true;
    }
    if (hit.type != HomeWidgetLayout::HitType::Carousel || hit.index >= bookCount) return false;
    const int tappedIndex = hit.index;
    if (mappedInput.lastTouchHeldMs() >= longPressMs) {
      popupBook = tappedIndex;
      updateRequired = true;
      return true;
    }
    openReaderFromCallback(RECENT_BOOKS.getBooks()[static_cast<size_t>(tappedIndex)].path, [] { onGoToHome(); });
    return true;
  }

  const ButtonBounds button = libraryButton();
  if (tapX >= button.x && tapX < button.x + button.width && tapY >= button.y && tapY < button.y + button.height) {
    onGoToLibrary("/");
    return true;
  }
  const HomeWidgetLayout::HitResult hit =
      widgetLayout.hitTest(HomeTheme::active(), carouselIndex, favoriteIndex, bookCount, tapX, tapY);
  if (hit.type == HomeWidgetLayout::HitType::Shortcut) return handleShortcut(hit.index);
  if (hit.type == HomeWidgetLayout::HitType::Recent) {
    openReaderFromCallback(RECENT_BOOKS.getBooks()[static_cast<size_t>(hit.index)].path, [] { onGoToHome(); });
    return true;
  }
  if (hit.type == HomeWidgetLayout::HitType::TodaysReading) {
    onGoToLibrary("/");
    return true;
  }
  return false;
}

bool Home::handleShortcutDrawerInput() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back) || mappedInput.wasTouchSwipeRight()) {
    shortcutDrawerOpen = false;
    updateRequired = true;
    return true;
  }
  if (!mappedInput.hasTouch()) return false;

  float tapNx = 0.0f;
  float tapNy = 0.0f;
  if (!mappedInput.wasTouchTapInScreen(renderer, tapNx, tapNy)) return false;
  const int tapX = static_cast<int>(tapNx * renderer.getScreenWidth());
  const int tapY = static_cast<int>(tapNy * renderer.getScreenHeight());
  const int drawerWidth = Sidebar::width(renderer);
  const int listTop = Sidebar::listTop();
  if (tapX >= 0 && tapX < drawerWidth && tapY >= listTop) {
    const int item = shortcutList.hitTest(tapX, tapY, 0, listTop, drawerWidth,
                                          renderer.getScreenHeight() - listTop, Sidebar::rowHeight);
    if (item >= 0) {
      shortcutDrawerOpen = false;
      updateRequired = true;
      handleShortcut(item);
      return true;
    }
  }

  if (tapX >= drawerWidth) {
    shortcutDrawerOpen = false;
    updateRequired = true;
  }
  return true;
}

void Home::drawShortcutDrawer() const {
  const int drawerWidth = Sidebar::width(renderer);
  const int listTop = Sidebar::listTop();
  Sidebar::renderFrame(renderer, "Shortcuts");
  shortcutList.render(0, listTop, drawerWidth, renderer.getScreenHeight() - listTop, Sidebar::rowHeight);
}

bool Home::handleShortcut(const int item) {
  switch (item) {
    case 0:
      openHomeSubPage(HomeSubPage::Section::Bookmarks);
      return true;
    case 1:
      openHomeSubPage(HomeSubPage::Section::Highlights);
      return true;
    case 2:
      openHomeSubPage(HomeSubPage::Section::Favorites);
      return true;
    case 3:
      onGoToStatistics();
      return true;
    case 4:
      openHomeSubPage(HomeSubPage::Section::Dictionary);
      return true;
    default:
      return false;
  }
}

void Home::popup() const {
  const std::vector<std::string> items = heatmapPopupOpen
                                             ? std::vector<std::string>{"View Report"}
                                             : (favoritePopupOpen ? std::vector<std::string>{"Remove favorite"}
                                                                   : std::vector<std::string>{"Remove Recent", "Delete cache"});
  const PopUpBounds box = PopUp::bounds(renderer, static_cast<int>(items.size()));
  PopUp::background(renderer, box);
  PopUp::title(renderer, box, heatmapPopupOpen ? "Heatmap" : (favoritePopupOpen ? "Favorite" : "Book"));
  PopUp::list(renderer, box, items, -1, 0);
  PopUp::border(renderer, box);
}

bool Home::popupInput() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
      (mappedInput.hasTouch() && mappedInput.wasTouchSwipeUp())) {
    favoritePopupOpen = false;
    heatmapPopupOpen = false;
    popupFavoritePath.clear();
    popupBook = -1;
    updateRequired = true;
    return true;
  }
  if (!mappedInput.hasTouch()) return false;

  float tapX = 0.0f;
  float tapY = 0.0f;
  if (!mappedInput.wasTouchTapInScreen(renderer, tapX, tapY)) return false;

  const PopUpBounds box = PopUp::bounds(renderer, heatmapPopupOpen || favoritePopupOpen ? 1 : 2);
  const int x = static_cast<int>(tapX * renderer.getScreenWidth());
  const int y = static_cast<int>(tapY * renderer.getScreenHeight());
  if (x < box.x || x >= box.x + box.width || y < box.y || y >= box.y + box.height) {
    favoritePopupOpen = false;
    heatmapPopupOpen = false;
    popupFavoritePath.clear();
    popupBook = -1;
    updateRequired = true;
    return true;
  }

  const int item = (y - box.y - box.header) / box.row;
  if (heatmapPopupOpen) {
    if (item == 0) {
      heatmapPopupOpen = false;
      updateRequired = true;
      onGoToHeatmapReport(heatmapPopupView);
    }
  } else if (favoritePopupOpen) {
    if (item == 0) {
      BOOK_STATE.toggleFavorite(popupFavoritePath);
      widgetLayout.invalidateFavorites();
      favoritePopupOpen = false;
      popupFavoritePath.clear();
      favoriteIndex = 0;
      updateRequired = true;
    }
  } else if (item == 0) {
    removeRecent();
  } else if (item == 1) {
    deleteCache();
  }
  return true;
}

void Home::removeRecent() {
  const std::vector<RecentBook>& books = RECENT_BOOKS.getBooks();
  if (popupBook >= 0 && popupBook < static_cast<int>(books.size())) {
    RECENT_BOOKS.removeBook(books[static_cast<size_t>(popupBook)].path);
  }
  popupBook = -1;
  if (carouselIndex >= RECENT_BOOKS.getCount()) carouselIndex = 0;
  updateRequired = true;
}

void Home::deleteCache() {
  const std::vector<RecentBook>& books = RECENT_BOOKS.getBooks();
  if (popupBook >= 0 && popupBook < static_cast<int>(books.size())) {
    const std::string path = cachePath(books[static_cast<size_t>(popupBook)]);
    if (SdMan.exists(path.c_str())) {
      int removed = 0;
      removeTree(path, removed);
    }
  }
  popupBook = -1;
  updateRequired = true;
}
