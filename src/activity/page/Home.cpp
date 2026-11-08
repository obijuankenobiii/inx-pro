/**
 * @file Home.cpp
 * @brief Empty home page for the new page redesign.
 */

#include "Home.h"

#include <GfxRenderer.h>
#include <SDCardManager.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

#include "HomeSubPage.h"
#include "components/global/Button.h"
#include "components/global/PopUp.h"
#include "state/BookState.h"
#include "state/RecentBooks.h"
#include "state/SystemSetting.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "system/ScreenComponents.h"

extern void openReaderFromCallback(const std::string& path, std::function<void()> returnToCaller);
extern void onGoToHome();
extern void onGoToLibrary(const std::string& path);
extern void openHomeSubPage(HomeSubPage::Section section);
extern void openHomeDescription(const std::string& bookPath, const std::string& cachePath);
extern void onGoToStatistics();
extern void onGoToHeatmapReport(HomeTheme::HeatmapView view);

namespace {

constexpr unsigned long longPressMs = 500;
void drawHomeClockAndBattery(const GfxRenderer& renderer) {
  constexpr int clockRightMargin = 20;
  constexpr int batteryClockGap = 10;
  const bool showPercentage = SETTINGS.hideBatteryPercentage != SystemSetting::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS;
  const int textWidth = showPercentage ? renderer.text.getWidth(systemFontIdForPointSize(8), "100%") : 0;
  const int batteryWidth = showPercentage
                               ? ScreenComponents::BATTERY_ICON_WIDTH + ScreenComponents::BATTERY_TEXT_GAP + textWidth
                               : ScreenComponents::BATTERY_ICON_WIDTH;
  const int top = navigation::Menu::topPadding + (navigation::Menu::iconSize - 18) / 2;
  if (!SETTINGS.showMenuClock || SETTINGS.hideMenuClock) {
    const int batteryLeft = renderer.getScreenWidth() - ScreenComponents::BATTERY_RIGHT_MARGIN - batteryWidth;
    ScreenComponents::drawBattery(renderer, batteryLeft, top, showPercentage);
    return;
  }

  int clockLeft = 0;
  const bool clockDrawn = ScreenComponents::drawMenuClockRightAligned(
      renderer, renderer.getScreenWidth() - clockRightMargin, top, &clockLeft);
  if (!clockDrawn) {
    const int batteryLeft = renderer.getScreenWidth() - ScreenComponents::BATTERY_RIGHT_MARGIN - batteryWidth;
    ScreenComponents::drawBattery(renderer, batteryLeft, top, showPercentage);
    return;
  }

  const int batteryLeft = clockLeft - batteryClockGap - batteryWidth;
  ScreenComponents::drawBattery(renderer, batteryLeft, top, showPercentage);
}

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
    : UiPage("Home", renderer, mappedInput), widgetLayout(renderer) {}

void Home::onEnter() {
  UiPage::onEnter();
  renderer.syncWriteBufferFromActive();
  carouselIndex = 0;
  favoriteIndex = 0;
  widgetPageIndex = 0;
  recentGridPageIndex = 0;
  flowIndex = 0;
  widgetLayout.invalidateFavorites();
  carouselThumbnailsPreloaded = false;
  popupBook = -1;
  favoritePopupOpen = false;
  heatmapPopupOpen = false;
  popupFavoritePath.clear();
  tapRejectLogged_ = false;
}

void Home::menu() {
  UiPage::menu();
#if INX_HAS_FRONTLIGHT || FREEINK_DEVICE_X4PRO || FREEINK_DEVICE_MURPHY_M4 || FREEINK_DEVICE_PICCO
  if (!isLightDrawerOpen()) drawHomeClockAndBattery(renderer);
#else
  drawHomeClockAndBattery(renderer);
#endif
}

void Home::title() const {
  const int font = systemFontIdForPointSize(16);
  const int textY = navigation::Menu::topPadding + (navigation::Menu::iconSize - renderer.text.getLineHeight(font)) / 2;
  renderer.text.render(font, navigation::Menu::leftMargin, textY, "Home", true, EpdFontFamily::BOLD);
}

ButtonBounds Home::libraryButton() const {
  const int width = Button::width(renderer, "Library", systemFontIdForPointSize(10));
  const int x = (renderer.getScreenWidth() - width) / 2;
  const int y = navigation::Menu::height + Carousel::kHeight / 2 + 14;
  return {x, y, width, Button::height};
}

void Home::loop() {
  if ((popupBook >= 0 || favoritePopupOpen || heatmapPopupOpen) && handlePopup()) return;
  if (menuInput()) return;
  if (isOpen()) {
    renderPage();
    return;
  }

  if (widgetLayout.needsRefresh(HomeTheme::active())) updateRequired = true;
  // Let the first Home frame reach the panel before doing carousel I/O and
  // thumbnail decoding. The preload is still done on the next idle loop, so
  // it remains warm for a swipe without extending the visible screen render.
  const bool renderedThisLoop = updateRequired;
  if (handleSwipe()) return;
  if (handleTap()) return;
  renderPage();
  if (!renderedThisLoop && !mappedInput.hasTouch()) preloadCarousel();
}

void Home::content() {
  widgetLayout.render(HomeTheme::active(), carouselIndex, favoriteIndex, widgetPageIndex, recentGridPageIndex, flowIndex);
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
  const bool swipeUp = mappedInput.wasTouchSwipeUp();
  const bool swipeDown = !swipeUp && mappedInput.wasTouchSwipeDown();
  if (swipeUp || swipeDown) {
    const int pageCount = widgetLayout.recentGridPageCount(HomeTheme::active(), widgetPageIndex,
                                                          RECENT_BOOKS.getCount());
    if (pageCount > 1) {
      recentGridPageIndex = (recentGridPageIndex + pageCount + (swipeUp ? 1 : -1)) % pageCount;
      updateRequired = true;
      renderPage();
      return true;
    }
    // A completed swipe owns this input cycle even when the current layout has
    // no page to move to. Do not let the same release fall through to a book
    // tap hit-test.
    return true;
  }
  float swipeNx = 0.0f;
  float swipeNy = 0.0f;
  const bool swipeLeftInScreen = mappedInput.wasTouchSwipeLeftInScreen(renderer, swipeNx, swipeNy);
  const bool swipeRightInScreen = !swipeLeftInScreen && mappedInput.wasTouchSwipeRightInScreen(renderer, swipeNx, swipeNy);
  const bool swipeLeft = swipeLeftInScreen || mappedInput.wasTouchSwipeLeft();
  const bool swipeRight = !swipeLeft && (swipeRightInScreen || mappedInput.wasTouchSwipeRight());
  if (!swipeLeft && !swipeRight) return false;

  const int swipeX = (swipeLeftInScreen || swipeRightInScreen)
                         ? static_cast<int>(swipeNx * renderer.getScreenWidth())
                         : renderer.getScreenWidth() / 2;
  const int swipeY = (swipeLeftInScreen || swipeRightInScreen)
                         ? static_cast<int>(swipeNy * renderer.getScreenHeight())
                         : navigation::Menu::height +
                               (renderer.getScreenHeight() - navigation::Menu::height - navigation::Menu::bottomInset()) / 2;
  const HomeWidgetLayout::SwipeTarget target =
      widgetLayout.horizontalSwipeTarget(HomeTheme::active(), widgetPageIndex, swipeX, swipeY);
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
  if (target == HomeWidgetLayout::SwipeTarget::Flow) {
    advanceFlow(swipeLeft ? 1 : -1);
    return true;
  }
  if (target == HomeWidgetLayout::SwipeTarget::WidgetPage) {
    const int count = widgetLayout.oneByOnePageCount(HomeTheme::active());
    if (count > 1) {
      widgetPageIndex = (widgetPageIndex + count + (swipeLeft ? 1 : -1)) % count;
      recentGridPageIndex = 0;
      updateRequired = true;
      renderPage();
    }
    return true;
  }
  // Keep all classified horizontal swipes out of handleTap(). This matters on
  // touch controllers that expose tap and swipe state on the same release
  // edge, and on edge swipes that start outside a widget's hit area.
  return true;
}

void Home::advanceCarousel(const int delta) {
  const int bookCount = widgetLayout.carouselBookCount(HomeTheme::active(), RECENT_BOOKS.getCount());
  if (bookCount <= 0) {
    carouselIndex = 0;
    return;
  }
  carouselIndex = (carouselIndex + bookCount + delta) % bookCount;
  updateRequired = true;
  renderPage();
}

void Home::advanceFlow(const int delta) {
  const int bookCount = RECENT_BOOKS.getCount();
  if (bookCount <= 0) {
    flowIndex = 0;
    return;
  }
  flowIndex = (flowIndex + bookCount + delta) % bookCount;
  updateRequired = true;
  renderPage();
}

void Home::preloadCarousel() {
  if (carouselThumbnailsPreloaded) return;
  widgetLayout.preloadCarousel(HomeTheme::active(), carouselIndex);
  carouselThumbnailsPreloaded = true;
}

bool Home::handleTap() {
  if (!mappedInput.hasTouch()) {
    tapRejectLogged_ = false;
    return false;
  }
  const bool touchActivity = mappedInput.wasTouchActivity();
  if (!touchActivity) tapRejectLogged_ = false;
  float tapNx = 0.0f;
  float tapNy = 0.0f;
  if (!mappedInput.wasTouchTapInScreen(renderer, tapNx, tapNy)) {
    if (!tapRejectLogged_ && touchActivity) {
      INX_SERIAL.printf("[%lu] [HOME-TAP] touch present but tap predicate rejected carouselIndex=%d\n", millis(),
                        carouselIndex);
      tapRejectLogged_ = true;
    }
    return false;
  }
  tapRejectLogged_ = false;
  const int tapX = static_cast<int>(tapNx * renderer.getScreenWidth());
  const int tapY = static_cast<int>(tapNy * renderer.getScreenHeight());
  const int bookCount = RECENT_BOOKS.getCount();
  const HomeWidgetLayout::HitResult favoriteHit =
      widgetLayout.hitTest(HomeTheme::active(), carouselIndex, favoriteIndex, bookCount, widgetPageIndex,
                           recentGridPageIndex, flowIndex, tapX, tapY);
  if (favoriteHit.type == HomeWidgetLayout::HitType::Heatmap) {
    if (mappedInput.lastTouchHeldMs() >= longPressMs) {
      heatmapPopupOpen = true;
      heatmapPopupView = HomeTheme::HeatmapView::Monthly;
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
  if (favoriteHit.type == HomeWidgetLayout::HitType::Library) {
    onGoToLibrary(widgetLayout.libraryFolder(HomeTheme::active(), favoriteHit.index));
    return true;
  }
  if (bookCount > 0) {
    const HomeWidgetLayout::HitResult hit =
        widgetLayout.hitTest(HomeTheme::active(), carouselIndex, favoriteIndex, bookCount, widgetPageIndex,
                             recentGridPageIndex, flowIndex, tapX, tapY);
    INX_SERIAL.printf(
        "[%lu] [HOME-TAP] tap=(%d,%d) norm=(%.3f,%.3f) books=%d carouselIndex=%d widgetPage=%d recentGridPage=%d "
        "hitType=%d hitIndex=%d held_ms=%lu\n",
        millis(), tapX, tapY, tapNx, tapNy, bookCount, carouselIndex, widgetPageIndex, recentGridPageIndex,
        static_cast<int>(hit.type), hit.index, mappedInput.lastTouchHeldMs());
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
    if (hit.type == HomeWidgetLayout::HitType::RecentGrid) {
      if (hit.index < 0 || hit.index >= bookCount) return true;
      if (mappedInput.lastTouchHeldMs() >= longPressMs) {
        popupBook = hit.index;
        updateRequired = true;
        return true;
      }
      openReaderFromCallback(RECENT_BOOKS.getBooks()[static_cast<size_t>(hit.index)].path, [] { onGoToHome(); });
      return true;
    }
    if (hit.type == HomeWidgetLayout::HitType::Flow) {
      if (hit.index < 0 || hit.index >= bookCount) return true;
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
    INX_SERIAL.printf("[%lu] [RECENT-CAROUSEL] opening index=%d path=%s\n", millis(), tappedIndex,
                      RECENT_BOOKS.getBooks()[static_cast<size_t>(tappedIndex)].path.c_str());
    openReaderFromCallback(RECENT_BOOKS.getBooks()[static_cast<size_t>(tappedIndex)].path, [] { onGoToHome(); });
    return true;
  }

  const ButtonBounds button = libraryButton();
  if (tapX >= button.x && tapX < button.x + button.width && tapY >= button.y && tapY < button.y + button.height) {
    onGoToLibrary("/");
    return true;
  }
  const HomeWidgetLayout::HitResult hit =
      widgetLayout.hitTest(HomeTheme::active(), carouselIndex, favoriteIndex, bookCount, widgetPageIndex,
                           recentGridPageIndex, flowIndex, tapX, tapY);
  if (hit.type == HomeWidgetLayout::HitType::Shortcut) return handleShortcut(hit.index);
  if (hit.type == HomeWidgetLayout::HitType::Recent) {
    openReaderFromCallback(RECENT_BOOKS.getBooks()[static_cast<size_t>(hit.index)].path, [] { onGoToHome(); });
    return true;
  }
  if (hit.type == HomeWidgetLayout::HitType::RecentGrid && hit.index >= 0 && hit.index < bookCount) {
    openReaderFromCallback(RECENT_BOOKS.getBooks()[static_cast<size_t>(hit.index)].path, [] { onGoToHome(); });
    return true;
  }
  if (hit.type == HomeWidgetLayout::HitType::TodaysReading) {
    onGoToLibrary("/");
    return true;
  }
  if (hit.type == HomeWidgetLayout::HitType::Library) {
    onGoToLibrary(widgetLayout.libraryFolder(HomeTheme::active(), hit.index));
    return true;
  }
  return false;
}

bool Home::handleShortcut(const int item) {
  if (SETTINGS.disableStatistics != 0 && item == 3) {
    // Removing Statistics shifts Dictionary from row 4 to row 3.
    openHomeSubPage(HomeSubPage::Section::Dictionary);
    return true;
  }
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
  const std::vector<std::string> items =
      heatmapPopupOpen
          ? std::vector<std::string>{"View Report"}
          : (favoritePopupOpen
                 ? std::vector<std::string>{"Remove favorite"}
                 : std::vector<std::string>{"View description", "Mark as completed", "Remove Recent", "Delete cache"});
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

  const PopUpBounds box = PopUp::bounds(renderer, heatmapPopupOpen || favoritePopupOpen ? 1 : 4);
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
    const std::vector<RecentBook>& books = RECENT_BOOKS.getBooks();
    if (popupBook >= 0 && popupBook < static_cast<int>(books.size())) {
      const RecentBook& book = books[static_cast<size_t>(popupBook)];
      const std::string bookPath = book.path;
      const std::string bookCachePath = cachePath(book);
      popupBook = -1;
      updateRequired = true;
      openHomeDescription(bookPath, bookCachePath);
    }
  } else if (item == 1) {
    markCompleted();
  } else if (item == 2) {
    removeRecent();
  } else if (item == 3) {
    deleteCache();
  }
  return true;
}

void Home::markCompleted() {
  const std::vector<RecentBook>& books = RECENT_BOOKS.getBooks();
  if (popupBook >= 0 && popupBook < static_cast<int>(books.size())) {
    const RecentBook& book = books[static_cast<size_t>(popupBook)];
    BOOK_STATE.setFinished(book.path, true, book.title);
    RECENT_BOOKS.updateProgress(book.path, 1.0f);
  }
  popupBook = -1;
  updateRequired = true;
}

void Home::removeRecent() {
  const std::vector<RecentBook>& books = RECENT_BOOKS.getBooks();
  if (popupBook >= 0 && popupBook < static_cast<int>(books.size())) {
    RECENT_BOOKS.removeBook(books[static_cast<size_t>(popupBook)].path);
  }
  popupBook = -1;
  if (carouselIndex >= widgetLayout.carouselBookCount(HomeTheme::active(), RECENT_BOOKS.getCount())) {
    carouselIndex = 0;
  }
  if (flowIndex >= RECENT_BOOKS.getCount()) flowIndex = 0;
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
