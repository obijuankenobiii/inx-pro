#include "Library.h"
#include "system/UiLayout.h"

#include <BitmapRender.h>
#include <GfxRenderer.h>
#include <SDCardManager.h>

#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <string>
#include <utility>

#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "components/global/PopUp.h"
#include "components/global/Sidebar.h"
#include "components/global/Toggle.h"
#include "components/library/AllBooksLibrary.h"
#include "images/Filter.h"
#include "images/Hamburger.h"
#include "images/LibraryViewGrid.h"
#include "images/LibraryViewList.h"
#include "images/LibraryViewThumb.h"
#include "images/LibraryFilterLeft.h"
#include "images/LibraryFilterRight.h"
#include "images/Refresh.h"
#include "images/SortAsc.h"
#include "images/SortDesc.h"
#include "state/BookState.h"
#include "state/EpubNotesIndex.h"
#include "state/RecentBooks.h"
#include "state/Session.h"
#include "state/SystemSetting.h"
#include "util/AuthorIndex.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "util/LibraryIndexRefresh.h"
#include "util/SdIoMutex.h"

extern void onGoToLibrary(const std::string& path);

namespace {
// Thumb page per folder path. File-scope, NOT a Library member: navigating calls onGoToLibrary(), which
// destroys this Library and constructs a fresh one for the new path, so any member state is lost exactly
// when it is needed. Bounded by folders actually visited.
std::map<std::string, int> gThumbPage;
}  // namespace
extern void openReaderFromCallback(const std::string& path, std::function<void()> returnToCaller);
extern void openSearchFromCallback(std::function<void()> returnToCaller);

namespace {

std::string parent(const std::string& value) {
  const size_t slash = value.find_last_of('/');
  if (slash == std::string::npos || slash == 0) return "/";
  return value.substr(0, slash);
}

std::string cleanPath(std::string value) {
  while (value.size() > 1 && value.back() == '/') value.pop_back();
  return value.empty() ? "/" : value;
}

bool isChild(const std::string& value, const std::string& base) {
  const std::string cleanBase = cleanPath(base);
  if (value == cleanBase) return false;
  const std::string prefix = cleanBase == "/" ? "/" : cleanBase + "/";
  if (value.compare(0, prefix.size(), prefix) != 0) return false;
  return value.find('/', prefix.size()) == std::string::npos;
}

std::string lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

char firstLetter(const std::string& value) {
  for (const unsigned char c : value) {
    if (std::isalpha(c)) return static_cast<char>(std::toupper(c));
  }
  return 0;
}

bool endsWith(const std::string& value, const char* suffix) {
  const size_t length = std::char_traits<char>::length(suffix);
  return value.size() >= length && value.compare(value.size() - length, length, suffix) == 0;
}

// Groups the extensions LibraryIndex actually scans for (see LibraryIndex.cpp) into the categories shown on
// the filter popup's Type tab - .md rides along with .txt and .xtch with .xtc since those are companion
// files for the same reader, not formats a user would think to filter separately.
bool matchesTypeFilter(const std::string& path, const std::string& category) {
  if (category.empty()) return true;
  const std::string value = lower(path);
  if (category == "epub") return endsWith(value, ".epub");
  if (category == "pdf") return endsWith(value, ".pdf");
  if (category == "txt") return endsWith(value, ".txt") || endsWith(value, ".md");
  if (category == "xtc") return endsWith(value, ".xtc") || endsWith(value, ".xtch");
  return true;
}

std::string dataPath(const std::string& bookPath) {
  const std::string value = lower(bookPath);
  const std::string hash = std::to_string(std::hash<std::string>{}(bookPath));
  if (endsWith(value, ".xtc") || endsWith(value, ".xtch")) return "/.metadata/xtc/" + hash;
  if (endsWith(value, ".txt") || endsWith(value, ".md")) return "/.system/txt_" + hash;
  if (endsWith(value, ".pdf")) return "/.metadata/pdf/" + hash;
  return "/.metadata/epub/" + hash;
}

bool removeTree(const std::string& path, int& removed) {
  if (!SdMan.exists(path.c_str())) return true;

  FsFile directory = SdMan.open(path.c_str());
  if (!directory) return false;
  if (!directory.isDirectory()) {
    directory.close();
    if (!SdMan.remove(path.c_str())) return false;
    ++removed;
    return true;
  }

  char name[128] = {};
  while (true) {
    FsFile entry = directory.openNextFile();
    if (!entry) break;
    const bool isDirectory = entry.isDirectory();
    entry.getName(name, sizeof(name));
    entry.close();

    const std::string child = path + "/" + name;
    const bool done = isDirectory ? removeTree(child, removed) : SdMan.remove(child.c_str());
    if (!done) {
      directory.close();
      return false;
    }
    ++removed;
    if ((removed & 7) == 0) {
      esp_task_wdt_reset();
      vTaskDelay(pdMS_TO_TICKS(1));
    }
  }
  directory.close();
  return SdMan.removeDir(path.c_str());
}

// Author metadata is not consistent across ebook sources. Group obvious variants such as
// "Jane Austen"/"Jane G. Austen" and "Austen, Jane" without changing the stored metadata.
std::string authorKey(const std::string& value) {
  const size_t comma = value.find(',');
  std::string ordered;
  if (comma == std::string::npos) {
    ordered = value;
  } else {
    ordered = value.substr(comma + 1) + " " + value.substr(0, comma);
  }

  std::vector<std::string> tokens;
  std::string token;
  for (const unsigned char character : ordered) {
    if (std::isalnum(character)) {
      token += static_cast<char>(std::tolower(character));
    } else if (!token.empty()) {
      tokens.push_back(std::move(token));
      token.clear();
    }
  }
  if (!token.empty()) tokens.push_back(std::move(token));
  while (tokens.size() > 1) {
    const std::string& last = tokens.back();
    if (last != "jr" && last != "sr" && last != "ii" && last != "iii" && last != "iv" && last != "v") break;
    tokens.pop_back();
  }
  if (tokens.empty()) return {};
  if (tokens.size() == 1) return tokens.front();
  return tokens.front() + "|" + tokens.back();
}

}  // namespace

Library::Library(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string path)
    : Page("Library", renderer, mappedInput),
      path(cleanPath(std::move(path))),
      grid(renderer, mappedInput, items, [this](const int index, const bool longPress) { select(index, longPress); },
           [this](const LibraryIndex::Book& book) { return isFavorite(book); },
           [this](const int x, const int y) { routeMenuAction(navigation::Menu::handleTap(x, y)); },
           [this](const LibraryIndex::Book& book) {
             return stateFilter == StateFilter::Author && book.type == LibraryIndex::Book::Type::FOLDER;
           }),
      list(renderer, mappedInput, items, [this](const int index, const bool longPress) { select(index, longPress); },
           [this](const LibraryIndex::Book& book) { return isFavorite(book); },
           [this](const int x, const int y) { routeMenuAction(navigation::Menu::handleTap(x, y)); },
           [this](const LibraryIndex::Book& book) {
             return stateFilter == StateFilter::Author && book.type == LibraryIndex::Book::Type::FOLDER;
           }),
      thumb(renderer, mappedInput, items, books,
            [this](const int index, const bool longPress) { select(index, longPress); },
            [this](const LibraryIndex::Book& book) { return isFavorite(book); },
            [this](const int x, const int y) { routeMenuAction(navigation::Menu::handleTap(x, y)); },
            [this](const LibraryIndex::Book& book) {
              return stateFilter == StateFilter::Author && book.type == LibraryIndex::Book::Type::FOLDER;
            }) {}

void Library::onEnter() {
  Page::onEnter();
  switch (SETTINGS.libraryMode) {
    case SystemSetting::LIBRARY_LIST:
      view = View::List;
      break;
    case SystemSetting::LIBRARY_GRID:
      view = View::Grid;
      break;
    case SystemSetting::LIBRARY_THUMBNAIL:
      view = View::Thumb;
      break;
    default:
      view = View::Grid;
      SETTINGS.libraryMode = SystemSetting::LIBRARY_GRID;
      SETTINGS.saveToFile();
      break;
  }
  sort = static_cast<Sort>(std::min<int>(SETTINGS.librarySortMode, sortCount() - 1));
  filter = 0;
  filterPage = 0;
  filterIndex = 9;
  filterTab = FilterTab::Title;
  typeFilter.clear();
  typeFilterIndex = 0;
  sortOpen = false;
  filterOpen = false;
  popupBook = -1;
  sidebarOpen = false;
  stateFilter = StateFilter::None;
  authorFolder.clear();
  authorFolderKey.clear();
  authorIndexAvailable = false;
  allBooksMode = path == "/" && SETTINGS.libraryViewMode == SystemSetting::LIBRARY_VIEW_BOOKS;
  if (path != "/" && SETTINGS.libraryViewMode != SystemSetting::LIBRARY_VIEW_FOLDERS) {
    SETTINGS.libraryViewMode = SystemSetting::LIBRARY_VIEW_FOLDERS;
    SETTINGS.saveToFile();
  }
  load();
}

void Library::load() {
  items.clear();
  resetViews();
  books.clear();
  favorites.clear();
  authorIndexAvailable = false;
  for (const BookState::Book& book : BOOK_STATE.getFavoriteBooks()) {
    favorites.insert(book.path);
  }
  if (stateFilter == StateFilter::Author) {
    std::vector<AuthorIndex::Entry> authorEntries;
    authorIndexAvailable = AuthorIndex::load(authorEntries);
    if (!authorIndexAvailable || !LibraryIndex::search("", items, LibraryIndex::all)) return;

    std::unordered_map<std::string, std::string> authorByPath;
    authorByPath.reserve(authorEntries.size());
    for (AuthorIndex::Entry& entry : authorEntries) {
      if (!entry.author.empty()) authorByPath.emplace(cleanPath(entry.path), std::move(entry.author));
    }
    if (authorFolder.empty()) {
      struct AuthorGroup {
        std::string display;
        int count = 0;
      };
      std::map<std::string, AuthorGroup> authorGroups;
      for (const auto& author : authorByPath) {
        const std::string key = authorKey(author.second);
        if (key.empty()) continue;
        AuthorGroup& group = authorGroups[key];
        ++group.count;
        if (group.display.empty() || author.second.size() < group.display.size()) group.display = author.second;
      }
      items.clear();
      items.reserve(authorGroups.size());
      for (const auto& author : authorGroups) {
        LibraryIndex::Book folder;
        folder.type = LibraryIndex::Book::Type::FOLDER;
        folder.path = "/.metadata/authors/" + std::to_string(std::hash<std::string>{}(author.first));
        folder.title = author.second.display;
        folder.folder = "/";
        folder.bookCount = static_cast<uint16_t>(std::min(author.second.count, 65535));
        folder.hasMetadata = true;
        items.push_back(std::move(folder));
      }
    } else {
      items.erase(std::remove_if(items.begin(), items.end(), [&authorByPath, this](LibraryIndex::Book& item) {
                    if (item.type != LibraryIndex::Book::Type::BOOK) return true;
                    const auto author = authorByPath.find(cleanPath(item.path));
                    if (author == authorByPath.end() || authorKey(author->second) != authorFolderKey) return true;
                    item.author = author->second;
                    return false;
                  }),
                  items.end());
    }
    books.clear();
  } else if (stateFilter != StateFilter::None) {
    if (!LibraryIndex::search("", items, LibraryIndex::all)) return;

    std::unordered_set<std::string> matchingPaths;
    if (stateFilter == StateFilter::Favorites) {
      matchingPaths = favorites;
    } else {
      const std::vector<BookState::Book> stateBooks =
          stateFilter == StateFilter::Reading ? BOOK_STATE.getReadingBooks() : BOOK_STATE.getFinishedBooks();
      matchingPaths.reserve(stateBooks.size());
      for (const BookState::Book& book : stateBooks) {
        matchingPaths.insert(cleanPath(book.path));
      }
    }

    items.erase(std::remove_if(items.begin(), items.end(), [&matchingPaths](const LibraryIndex::Book& item) {
                  return item.type != LibraryIndex::Book::Type::BOOK ||
                         matchingPaths.find(cleanPath(item.path)) == matchingPaths.end();
                }),
                items.end());
    books.clear();
  } else if (allBooksMode) {
    if (!AllBooksLibrary::load(items)) return;
    // All Books contains no folder rows, so Thumb never needs a second catalog for folder covers/counts.
    books.clear();
  } else {
    if (!LibraryIndex::search("", books, LibraryIndex::all)) return;

    for (const auto& item : books) {
      if (item.type == LibraryIndex::Book::Type::BOOK) {
        if (parent(item.path) == path) items.push_back(item);
      } else if (isChild(item.path, path)) {
        items.push_back(item);
      }
    }
  }

  if (SETTINGS.hideFinishedBooks != 0 && stateFilter == StateFilter::None) {
    std::unordered_set<std::string> finishedPaths;
    for (const BookState::Book& book : BOOK_STATE.getFinishedBooks()) {
      finishedPaths.insert(cleanPath(book.path));
    }
    items.erase(std::remove_if(items.begin(), items.end(), [&finishedPaths](const LibraryIndex::Book& item) {
                  return item.type == LibraryIndex::Book::Type::BOOK &&
                         finishedPaths.find(cleanPath(item.path)) != finishedPaths.end();
                }),
                items.end());
  }

  if (filter != 0) {
    items.erase(std::remove_if(items.begin(), items.end(), [this](const LibraryIndex::Book& item) {
                  return firstLetter(item.title) != filter;
                }),
                items.end());
  }

  if (!typeFilter.empty()) {
    // Folders are exempt - filtering to "PDF" should narrow which books show up, not strand the user unable
    // to navigate into a subfolder that might contain PDFs further down.
    items.erase(std::remove_if(items.begin(), items.end(), [this](const LibraryIndex::Book& item) {
                  return item.type == LibraryIndex::Book::Type::BOOK && !matchesTypeFilter(item.path, typeFilter);
                }),
                items.end());
  }

  // The secondary catalog is only needed when the visible list contains folders. Releasing it for
  // a flat library avoids retaining a second copy of thousands of Book strings on the heap.
  if (std::none_of(items.begin(), items.end(), [](const LibraryIndex::Book& item) {
        return item.type == LibraryIndex::Book::Type::FOLDER;
      })) {
    std::vector<LibraryIndex::Book>().swap(books);
  }

  std::stable_sort(items.begin(), items.end(), [this](const LibraryIndex::Book& left,
                                                       const LibraryIndex::Book& right) {
    const std::string leftTitle = lower(left.title);
    const std::string rightTitle = lower(right.title);
    const std::string leftFolder = lower(left.folder.empty() ? parent(left.path) : left.folder);
    const std::string rightFolder = lower(right.folder.empty() ? parent(right.path) : right.folder);
    if (sort == Sort::AuthorAZ || sort == Sort::AuthorZA) {
      const std::string leftAuthor = lower(left.author);
      const std::string rightAuthor = lower(right.author);
      if (leftAuthor != rightAuthor) {
        return sort == Sort::AuthorAZ ? leftAuthor < rightAuthor : leftAuthor > rightAuthor;
      }
    }
    if (sort == Sort::FolderAZ || sort == Sort::FolderZA) {
      if (leftFolder != rightFolder) {
        return sort == Sort::FolderAZ ? leftFolder < rightFolder : leftFolder > rightFolder;
      }
    }
    if (sort == Sort::TitleZA || sort == Sort::FolderZA || sort == Sort::AuthorZA) return leftTitle > rightTitle;
    return leftTitle < rightTitle;
  });
  resetViews();
  thumb.setRoot(path == "/" && !allBooksMode);
  restoreThumbPage();
  if (view == View::Thumb) thumb.load();
}

void Library::open(const int index) {
  if (index < 0 || index >= static_cast<int>(items.size())) return;
  const LibraryIndex::Book& item = items[static_cast<size_t>(index)];
  if (item.type == LibraryIndex::Book::Type::FOLDER) {
    if (stateFilter == StateFilter::Author && authorFolder.empty()) {
      authorFolder = item.title;
      authorFolderKey = authorKey(item.title);
      thumb.setPage(0);
      load();
      updateRequired = true;
      return;
    }
    gThumbPage[path] = thumb.currentPage();  // remember where we were before this Library is destroyed
    onGoToLibrary(item.path);
  } else {
    const std::string libraryPath = path;
    openReaderFromCallback(item.path, [libraryPath] { onGoToLibrary(libraryPath); });
  }
}

void Library::loop() {
  if (refreshing && !LibraryIndexRefresh::isRunning()) {
    refreshing = false;
    load();
    updateRequired = true;
  }

  if (popupBook >= 0) {
    if (popupBook >= static_cast<int>(items.size())) {
      popupBook = -1;
      updateRequired = true;
    } else if (popupInput()) {
      return;
    }
    renderPage();
    return;
  }

  if (sidebarOpen) {
    if (handleSidebarInput()) return;
    renderPage();
    return;
  }

  if (handleSidebarTap()) return;

  const bool horizontalSwipe = mappedInput.wasTouchSwipeLeft() || mappedInput.wasTouchSwipeRight();

  // Horizontal swipes are library navigation only. Handle them before the shared
  // page shell so a root-level swipe is consumed instead of reaching Home.
  if (!filterOpen && !sortOpen && !isOpen() && horizontalSwipe) {
    if (stateFilter == StateFilter::Author && !authorFolder.empty() && mappedInput.wasTouchSwipeRight()) {
      authorFolder.clear();
      authorFolderKey.clear();
      thumb.setPage(0);
      load();
      updateRequired = true;
      return;
    }
    if (path != "/") {
      gThumbPage[path] = thumb.currentPage();
      onGoToLibrary(parent(path));
    }
    return;
  }

  if (menuInput()) return;

  if (isOpen()) {
    renderPage();
    return;
  }

  if (filterOpen) {
    if (filterTab == FilterTab::Title && mappedInput.wasTouchSwipeLeft()) {
      changeFilterPage(-1);
      return;
    }
    if (filterTab == FilterTab::Title && mappedInput.wasTouchSwipeRight()) {
      changeFilterPage(1);
      return;
    }
    if (mappedInput.hasTouch()) {
      float tapNx = 0.0f;
      float tapNy = 0.0f;
      if (mappedInput.wasTouchTapInScreen(renderer, tapNx, tapNy)) {
        handleFilterTap(static_cast<int>(tapNx * renderer.getScreenWidth()),
                        static_cast<int>(tapNy * renderer.getScreenHeight()));
        return;
      }
    }
    renderPage();
    return;
  }

  if (sortOpen) {
    if (mappedInput.hasTouch()) {
      float tapNx = 0.0f;
      float tapNy = 0.0f;
      if (mappedInput.wasTouchTapInScreen(renderer, tapNx, tapNy)) {
        handleSortTap(static_cast<int>(tapNx * renderer.getScreenWidth()),
                      static_cast<int>(tapNy * renderer.getScreenHeight()));
        return;
      }
    }
    renderPage();
    return;
  }

  bool handled = false;
  switch (view) {
    case View::List:
      handled = list.handleInput();
      break;
    case View::Grid:
      handled = grid.handleInput();
      break;
    case View::Thumb:
      handled = thumb.handleInput();
      break;
  }
  if (handled) {
    updateRequired = true;
    return;
  }
  if (updateRequired) {
    renderPage();
    return;
  }
  if (view == View::Thumb && thumb.loadNext()) {
    updateRequired = true;
  } else if (view == View::Thumb) {
    // Visible page is settled - use the idle time to warm the next page's covers. Deliberately does NOT
    // set updateRequired: nothing on screen changes, so this must not trigger a repaint/refresh.
    thumb.prefetchNextPage();
  }
  renderPage();
}

void Library::content() {
  if (items.empty()) {
    const char* message = stateFilter == StateFilter::Author && !authorIndexAvailable
                              ? "Generate authors in Settings first"
                              : (LibraryIndex::hasIndex() ? "No books in this folder" : "Build the library index first");
    renderer.text.centered(systemFontId(), renderer.getScreenHeight() / 2, message);
    return;
  }
  switch (view) {
    case View::List:
      list.render();
      break;
    case View::Grid:
      grid.render();
      break;
    case View::Thumb:
      thumb.render();
      break;
  }
}

void Library::title() const {
  renderer.bitmap.icon(Hamburger, navigation::Menu::leftMargin, navigation::Menu::topPadding,
                       navigation::Menu::iconSize, navigation::Menu::iconSize);
  const int font = MONTSERRAT_16_FONT_ID;
  const int textY = navigation::Menu::topPadding +
                    (navigation::Menu::iconSize - renderer.text.getLineHeight(font)) / 2;
  renderer.text.render(font, navigation::Menu::leftMargin + navigation::Menu::iconSize + 12, textY, "Library", true,
                       EpdFontFamily::BOLD);
}

void Library::select(const int index, const bool longPress) {
  if (index < 0 || index >= static_cast<int>(items.size())) return;
  if (longPress && items[static_cast<size_t>(index)].type == LibraryIndex::Book::Type::BOOK) {
    popupBook = index;
    sortOpen = false;
    filterOpen = false;
    updateRequired = true;
    return;
  }
  open(index);
}

void Library::popup() const {
  const LibraryIndex::Book& book = items[static_cast<size_t>(popupBook)];
  const std::vector<std::string> actions = {isFavorite(book) ? "Remove favorite" : "Mark as favorite",
                                            "Delete Book", "Reset"};
  const PopUpBounds box = PopUp::bounds(renderer, static_cast<int>(actions.size()));
  PopUp::background(renderer, box);
  PopUp::title(renderer, box, book.title.empty() ? "Book" : book.title);
  PopUp::list(renderer, box, actions, -1, 0);
  PopUp::border(renderer, box);
}

bool Library::popupInput() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
      (mappedInput.hasTouch() && mappedInput.wasTouchSwipeUp())) {
    popupBook = -1;
    updateRequired = true;
    return true;
  }
  if (!mappedInput.hasTouch()) return false;

  float tapX = 0.0f;
  float tapY = 0.0f;
  if (!mappedInput.wasTouchTapInScreen(renderer, tapX, tapY)) return false;

  const PopUpBounds box = PopUp::bounds(renderer, 3);
  const int x = static_cast<int>(tapX * renderer.getScreenWidth());
  const int y = static_cast<int>(tapY * renderer.getScreenHeight());
  if (x < box.x || x >= box.x + box.width || y < box.y || y >= box.y + box.height) {
    popupBook = -1;
    updateRequired = true;
    return true;
  }

  const int action = (y - box.y - box.header) / box.row;
  if (action < 0 || action >= 3) return true;
  const LibraryIndex::Book book = items[static_cast<size_t>(popupBook)];
  if (action == 0) {
    markFavorite(book);
  } else if (action == 1) {
    erase(book);
  } else {
    reset(book);
  }
  return true;
}

bool Library::isFavorite(const LibraryIndex::Book& book) const {
  return book.type == LibraryIndex::Book::Type::BOOK && favorites.find(book.path) != favorites.end();
}

void Library::markFavorite(const LibraryIndex::Book& book) {
  const bool wasFavorite = isFavorite(book);
  BOOK_STATE.toggleFavorite(book.path, book.title);
  if (wasFavorite) {
    favorites.erase(book.path);
  } else {
    favorites.insert(book.path);
  }
  popupBook = -1;
  updateRequired = true;
}

void Library::reset(const LibraryIndex::Book& book) {
  int removed = 0;
  {
    SdIoMutex::Lock lock;
    removeTree(dataPath(book.path), removed);
  }
  RECENT_BOOKS.removeBook(book.path);
  BOOK_STATE.setReading(book.path, false);
  BOOK_STATE.setFinished(book.path, false);
  if (APP_STATE.lastRead == book.path) {
    APP_STATE.lastRead.clear();
    APP_STATE.saveToFile();
  }
  EpubNotesIndex::invalidate();
  popupBook = -1;
  load();
  updateRequired = true;
}

void Library::erase(const LibraryIndex::Book& book) {
  bool deleted = false;
  {
    SdIoMutex::Lock lock;
    deleted = !SdMan.exists(book.path.c_str()) || SdMan.remove(book.path.c_str());
  }
  if (!deleted) {
    popupBook = -1;
    updateRequired = true;
    return;
  }

  reset(book);
  BOOK_STATE.removeBook(book.path);
  RECENT_BOOKS.removeBook(book.path);
  favorites.erase(book.path);

  items.erase(std::remove_if(items.begin(), items.end(), [&book](const LibraryIndex::Book& item) {
                return item.path == book.path;
              }),
              items.end());
  books.erase(std::remove_if(books.begin(), books.end(), [&book](const LibraryIndex::Book& item) {
                return item.path == book.path;
              }),
              books.end());
  resetViews();
  restoreThumbPage();
  if (view == View::Thumb) thumb.load();
  refreshing = true;
  LibraryIndexRefresh::start(renderer, this);
  updateRequired = true;
}

void Library::search() {
  const std::string libraryPath = path;
  openSearchFromCallback([libraryPath] { onGoToLibrary(libraryPath); });
}

void Library::refresh() {
  if (LibraryIndexRefresh::isRunning()) return;
  refreshing = true;
  LibraryIndexRefresh::start(renderer, this);
}

int Library::buttonX(const int index) const {
  const int right = renderer.getScreenWidth() - 20;
  const int refreshX = right - navigation::Menu::iconSize;
  const int filterX = refreshX - buttonGap - buttonSize;
  const int sortX = filterX - buttonGap - buttonSize;
  const int viewX = sortX - buttonGap - buttonSize;
  switch (index) {
    case 0:
      return viewX;
    case 1:
      return sortX;
    case 2:
      return filterX;
    case 3:
    default:
      return refreshX;
  }
}

int Library::buttonY() const { return navigation::Menu::topPadding; }

void Library::center() const {
  const bool sortSelected = sortOpen;
  const bool filterSelected = filterOpen;
  const uint8_t* sortIcon = sort == Sort::TitleZA || sort == Sort::FolderZA ? SortDesc : SortAsc;
  constexpr int selectedPadding = 4;

  if (sortSelected) {
    renderer.rectangle.fill(buttonX(1) - selectedPadding, buttonY() - selectedPadding,
                            buttonSize + selectedPadding * 2, buttonSize + selectedPadding * 2, true);
  }
  if (filterSelected) {
    renderer.rectangle.fill(buttonX(2) - selectedPadding, buttonY() - selectedPadding,
                            buttonSize + selectedPadding * 2, buttonSize + selectedPadding * 2, true);
  }
  renderer.bitmap.icon(viewIcon(), buttonX(0), buttonY(), buttonSize, buttonSize,
                       BitmapRender::Orientation::None, false);
  renderer.bitmap.icon(sortIcon, buttonX(1), buttonY(), buttonSize, buttonSize,
                       BitmapRender::Orientation::None, sortSelected);
  renderer.bitmap.icon(Filter, buttonX(2), buttonY(), buttonSize, buttonSize,
                       BitmapRender::Orientation::None, filterSelected);
  renderer.bitmap.icon(Refresh, buttonX(3), buttonY(), buttonSize, buttonSize);
}

bool Library::handleSidebarTap() {
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
    sidebarOpen = true;
    updateRequired = true;
    return true;
  }

  mappedInput.restoreTouchTapInScreen(renderer, tapNx, tapNy);
  return false;
}

bool Library::handleSidebarInput() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back) || mappedInput.wasTouchSwipeRight()) {
    sidebarOpen = false;
    updateRequired = true;
    return true;
  }
  if (!mappedInput.hasTouch()) return false;

  float tapNx = 0.0f;
  float tapNy = 0.0f;
  if (!mappedInput.wasTouchTapInScreen(renderer, tapNx, tapNy)) return false;

  const int tapX = static_cast<int>(tapNx * renderer.getScreenWidth());
  const int tapY = static_cast<int>(tapNy * renderer.getScreenHeight());
  const int drawerWidth = std::min(320, renderer.getScreenWidth() * 3 / 4);
  if (tapX >= drawerWidth) {
    sidebarOpen = false;
    updateRequired = true;
    return true;
  }

  const int item = Sidebar::hitTest(renderer, tapX, tapY, 6);
  if (item == 0) {
    stateFilter = StateFilter::None;
    if (allBooksMode) {
      SETTINGS.libraryViewMode = SystemSetting::LIBRARY_VIEW_FOLDERS;
      SETTINGS.saveToFile();
      allBooksMode = false;
      sortOpen = false;
      filterOpen = false;
      popupBook = -1;
      thumb.setPage(0);
      load();
    } else {
      // Keep the currently selected List/Grid/Thumbnail layout when switching the data source.
      const View selectedView = view;
      SETTINGS.libraryMode = selectedView == View::List
                                 ? SystemSetting::LIBRARY_LIST
                                 : (selectedView == View::Grid ? SystemSetting::LIBRARY_GRID
                                                               : SystemSetting::LIBRARY_THUMBNAIL);
      SETTINGS.libraryViewMode = SystemSetting::LIBRARY_VIEW_BOOKS;
      SETTINGS.saveToFile();
      sidebarOpen = false;
      updateRequired = true;
      if (path != "/") {
        gThumbPage[path] = thumb.currentPage();
        onGoToLibrary("/");
      } else {
        allBooksMode = true;
        sortOpen = false;
        filterOpen = false;
        popupBook = -1;
        thumb.setPage(0);
        load();
        view = selectedView;
      }
      return true;
    }
    sidebarOpen = false;
    updateRequired = true;
  } else if (item == 1 || item == 2 || item == 3) {
    stateFilter = item == 1 ? StateFilter::Favorites
                            : (item == 2 ? StateFilter::Reading : StateFilter::Finished);
    sortOpen = false;
    filterOpen = false;
    popupBook = -1;
    thumb.setPage(0);
    sidebarOpen = false;
    load();
    updateRequired = true;
  } else if (item == 4) {
    stateFilter = StateFilter::Author;
    authorFolder.clear();
    authorFolderKey.clear();
    sort = Sort::AuthorAZ;
    SETTINGS.librarySortMode = static_cast<uint8_t>(sort);
    SETTINGS.saveToFile();
    sortOpen = false;
    filterOpen = false;
    popupBook = -1;
    thumb.setPage(0);
    sidebarOpen = false;
    load();
    updateRequired = true;
  }
  return true;
}

void Library::drawSidebar() const {
  const char* labels[] = {allBooksMode ? "Folders" : "All books", "Favorites", "Reading", "Finished", "Author"};
  Sidebar::renderFrame(renderer, "Library");
  Sidebar::renderTextList(renderer, labels, 5);
}

navigation::Menu::Action Library::centerTap(const int tapX, const int tapY) const {
  // Keep the 40px icon unchanged, but give refresh a larger touch-only target.
  // The extra area sits in the existing gap to the filter button.
  if (tapX >= buttonX(3) - refreshTouchPadding && tapX < buttonX(3) + buttonSize + refreshTouchPadding &&
      tapY >= buttonY() - refreshTouchPadding && tapY < buttonY() + buttonSize + refreshTouchPadding) {
    return navigation::Menu::Action::Refresh;
  }
  if (tapY < buttonY() || tapY >= buttonY() + buttonSize) return navigation::Menu::Action::None;
  if (tapX >= buttonX(0) && tapX < buttonX(0) + buttonSize) return navigation::Menu::Action::View;
  if (tapX >= buttonX(1) && tapX < buttonX(1) + buttonSize) return navigation::Menu::Action::Sort;
  if (tapX >= buttonX(2) && tapX < buttonX(2) + buttonSize) return navigation::Menu::Action::Filter;
  return navigation::Menu::Action::None;
}

bool Library::menuAction(const navigation::Menu::Action action) {
  if (action == navigation::Menu::Action::View) {
    switch (view) {
      case View::List:
        view = View::Grid;
        break;
      case View::Grid:
        view = View::Thumb;
        break;
      case View::Thumb:
        view = View::List;
        break;
    }
    SETTINGS.libraryMode = view == View::List
                               ? SystemSetting::LIBRARY_LIST
                               : (view == View::Grid ? SystemSetting::LIBRARY_GRID
                                                     : SystemSetting::LIBRARY_THUMBNAIL);
    SETTINGS.saveToFile();
    resetViews();
    restoreThumbPage();
    if (view == View::Thumb) thumb.load();
    updateRequired = true;
    return true;
  }
  if (action == navigation::Menu::Action::Sort) {
    sortOpen = !sortOpen;
    filterOpen = false;
    updateRequired = true;
    return true;
  }
  if (action == navigation::Menu::Action::Filter) {
    filterOpen = !filterOpen;
    sortOpen = false;
    updateRequired = true;
    return true;
  }
  return false;
}

void Library::menu() {
  Page::menu();
  if (sortOpen) sortDropdown();
  if (filterOpen) filterPopup();
  if (popupBook >= 0 && popupBook < static_cast<int>(items.size())) popup();
  if (sidebarOpen) drawSidebar();
}

int Library::sortCount() const { return 6; }

int Library::sortIndex() const { return static_cast<int>(sort); }

const uint8_t* Library::viewIcon() const {
  switch (view) {
    case View::List:
      return LibraryViewList;
    case View::Grid:
      return LibraryViewGrid;
    case View::Thumb:
      return LibraryViewThumb;
    default:
      return LibraryViewGrid;
  }
}

void Library::restoreThumbPage() {
  const auto remembered = gThumbPage.find(path);
  thumb.setPage(remembered != gThumbPage.end() ? remembered->second : 0);
}

void Library::resetViews() {
  grid.reset();
  list.reset();
  thumb.reset();
}

void Library::sortDropdown() const {
  constexpr int rowHeight = UiLayout::LIST_ITEM_HEIGHT;
  constexpr int width = 250;
  constexpr int leftPadding = 20;
  constexpr int rightPadding = 40;
  const int height = sortCount() * rowHeight + 1;
  const int x = std::max(0, buttonX(1) + buttonSize - width);
  const int y = navigation::Menu::height;

  renderer.rectangle.fill(x, y, width, height, false);
  const char* names[] = {"Title", "Title", "Folder", "Folder", "Author", "Author"};
  const char* directions[] = {"A-Z", "Z-A", "A-Z", "Z-A", "A-Z", "Z-A"};
  for (int index = 0; index < sortCount(); ++index) {
    const int rowY = y + index * rowHeight;
    const bool selected = index == sortIndex();
    if (selected) {
      renderer.rectangle.fill(x, rowY, width, rowHeight, true);
    }
    const int font = systemFontId();
    const int textY = rowY + (rowHeight - renderer.text.getLineHeight(font)) / 2;
    renderer.text.render(font, x + leftPadding, textY, names[index], !selected, EpdFontFamily::REGULAR);
    const int directionFont = MONTSERRAT_8_FONT_ID;
    const int directionWidth = renderer.text.getWidth(directionFont, directions[index]);
    const int directionY = rowY + (rowHeight - renderer.text.getLineHeight(directionFont)) / 2;
    renderer.text.render(directionFont, x + width - rightPadding - directionWidth, directionY, directions[index],
                         !selected, EpdFontFamily::REGULAR);
    if (index + 1 < sortCount()) {
      renderer.line.render(x + 10, rowY + rowHeight, x + width - 10, rowY + rowHeight, !selected,
                           LineRender::Style::Dotted);
    }
  }
  renderer.rectangle.render(x, y, width, height, true);
}

void Library::handleSortTap(const int tapX, const int tapY) {
  constexpr int rowHeight = UiLayout::LIST_ITEM_HEIGHT;
  constexpr int width = 250;
  const int height = sortCount() * rowHeight + 1;
  const int x = std::max(0, buttonX(1) + buttonSize - width);
  const int y = navigation::Menu::height;
  if (tapX >= x && tapX < x + width && tapY >= y && tapY < y + height) {
    applySort((tapY - y) / rowHeight);
    return;
  }
  sortOpen = false;
  updateRequired = true;
}

void Library::applySort(const int index) {
  if (index < 0 || index >= sortCount()) return;
  sort = static_cast<Sort>(index);
  SETTINGS.librarySortMode = static_cast<uint8_t>(index);
  SETTINGS.saveToFile();
  sortOpen = false;
  load();
  updateRequired = true;
}

char Library::letter(const int index) const {
  if (index == 9) return '*';
  const int value = filterPage * 9 + index;
  return value >= 0 && value < 26 ? static_cast<char>('A' + value) : 0;
}

void Library::changeFilterPage(const int delta) {
  filterPage = (filterPage + (delta < 0 ? 2 : 1)) % 3;
  if (filterIndex != 9 && letter(filterIndex) == 0) filterIndex = 9;
  updateRequired = true;
}

void Library::applyFilter(const int index) {
  const char selected = letter(index);
  if (selected == 0) return;
  filterIndex = index;
  filter = selected == '*' ? 0 : selected;
  filterOpen = false;
  load();
  updateRequired = true;
}

const char* Library::typeFilterLabel(const int index) {
  switch (index) {
    case 1:
      return "EPUB";
    case 2:
      return "PDF";
    case 3:
      return "TXT";
    case 4:
      return "XTC";
    default:
      return "All";
  }
}

const char* Library::typeFilterCategory(const int index) {
  switch (index) {
    case 1:
      return "epub";
    case 2:
      return "pdf";
    case 3:
      return "txt";
    case 4:
      return "xtc";
    default:
      return "";
  }
}

void Library::applyTypeFilter(const int index) {
  typeFilterIndex = index;
  typeFilter = typeFilterCategory(index);
  filterOpen = false;
  load();
  updateRequired = true;
}

void Library::filterPopup() const {
  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  const int panelWidth = std::min(screenWidth - 48, 330);
  constexpr int panelHeight = 370;
  const int panelX = (screenWidth - panelWidth) / 2;
  const int panelY = std::max(navigation::Menu::height + 20, (screenHeight - panelHeight) / 2);
  constexpr int padding = 18;
  constexpr int titleHeight = 28;
  constexpr int tabContentGap = 20;
  constexpr int gap = 10;
  constexpr int allRowGap = 10;       // gap between the 3x3 grid and the All row
  constexpr int allBottomMargin = 20;  // gap between the All row and the panel's bottom border
  const int gridY = panelY + padding + titleHeight + tabContentGap;
  const int cellWidth = (panelWidth - padding * 2 - gap * 2) / 3;
  // 4 cell-sized rows total (3 grid rows + the All row), plus the gaps between/around them.
  const int cellHeight =
      (panelHeight - padding * 2 - titleHeight - tabContentGap - gap * 2 - allRowGap - allBottomMargin) / 4;
  const int selectorSize = std::min(cellWidth, cellHeight);
  const int font = systemFontId();

  renderer.rectangle.fill(panelX, panelY, panelWidth, panelHeight, false);
  renderer.rectangle.render(panelX, panelY, panelWidth, panelHeight, true);

  // Title | Type | Options header, replacing the old single "Filter" label - each tab gets an equal-width
  // column of the panel so hit-testing (handleFilterTap()) is just tapX divided by the column width.
  static constexpr const char* kTabLabels[] = {"Title", "Type", "Options"};
  const int tabAreaX = panelX + padding;
  const int tabAreaWidth = panelWidth - padding * 2;
  const int tabColumnWidth = tabAreaWidth / 3;
  const int tabTextY = panelY + padding + 3;
  for (int tab = 0; tab < 3; ++tab) {
    const bool selected = static_cast<int>(filterTab) == tab;
    const EpdFontFamily::Style style = selected ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
    const int columnX = tabAreaX + tab * tabColumnWidth;
    const int textWidth = renderer.text.getWidth(font, kTabLabels[tab], style);
    const int textX = columnX + (tabColumnWidth - textWidth) / 2;
    renderer.text.render(font, textX, tabTextY, kTabLabels[tab], true, style);
  }

  if (filterTab == FilterTab::Title) {
    for (int index = 0; index < 9; ++index) {
      const char value = letter(index);
      if (value == 0) continue;
      const int column = index % 3;
      const int row = index / 3;
      const int cellX = panelX + padding + column * (cellWidth + gap);
      const int cellY = gridY + row * (cellHeight + gap);
      const int selectorX = cellX + (cellWidth - selectorSize) / 2;
      const int selectorY = cellY + (cellHeight - selectorSize) / 2;
      const bool selected = index == filterIndex;
      if (selected) renderer.rectangle.fill(selectorX, selectorY, selectorSize, selectorSize, true, true, true);
      char label[2] = {value, '\0'};
      const int textWidth = renderer.text.getWidth(font, label, EpdFontFamily::BOLD);
      const int textY = cellY + (cellHeight - renderer.text.getLineHeight(font)) / 2;
      renderer.text.render(font, cellX + (cellWidth - textWidth) / 2, textY, label, !selected,
                           EpdFontFamily::BOLD);
    }

    const int allY = gridY + 3 * (cellHeight + gap) + allRowGap;
    const int allX = panelX + (panelWidth - selectorSize) / 2;
    constexpr int arrowSize = 30;
    const int arrowY = allY + (selectorSize - arrowSize) / 2;
    const int leftX = panelX + padding;
    const int rightX = panelX + panelWidth - padding - arrowSize;
    renderer.bitmap.icon(LibraryFilterLeft, leftX, arrowY, arrowSize, arrowSize);
    renderer.bitmap.icon(LibraryFilterRight, rightX, arrowY, arrowSize, arrowSize);
    const bool allSelected = filterIndex == 9;
    if (allSelected) renderer.rectangle.fill(allX, allY, selectorSize, selectorSize, true, true, true);
    const int allWidth = renderer.text.getWidth(font, "All", EpdFontFamily::BOLD);
    const int allYText = allY + (selectorSize - renderer.text.getLineHeight(font)) / 2;
    renderer.text.render(font, panelX + (panelWidth - allWidth) / 2, allYText, "All", !allSelected,
                         EpdFontFamily::BOLD);
  } else if (filterTab == FilterTab::Type) {
    // Same 3-column cell grid the Title tab uses (cellWidth/cellHeight/selectorSize), just with 5 type
    // options instead of 9 letters - row 1's third cell is simply left empty.
    constexpr int typeOptionCount = 5;
    for (int index = 0; index < typeOptionCount; ++index) {
      const int column = index % 3;
      const int row = index / 3;
      const int cellX = panelX + padding + column * (cellWidth + gap);
      const int cellY = gridY + row * (cellHeight + gap);
      const int selectorX = cellX + (cellWidth - selectorSize) / 2;
      const int selectorY = cellY + (cellHeight - selectorSize) / 2;
      const bool selected = index == typeFilterIndex;
      if (selected) renderer.rectangle.fill(selectorX, selectorY, selectorSize, selectorSize, true, true, true);
      const char* label = typeFilterLabel(index);
      const int textWidth = renderer.text.getWidth(font, label, EpdFontFamily::BOLD);
      const int textY = cellY + (cellHeight - renderer.text.getLineHeight(font)) / 2;
      renderer.text.render(font, cellX + (cellWidth - textWidth) / 2, textY, label, !selected,
                           EpdFontFamily::BOLD);
    }
  } else {
    const int rowY = gridY;
    const int rowHeight = cellHeight;
    const int textY = rowY + (rowHeight - renderer.text.getLineHeight(font)) / 2;
    renderer.text.render(font, panelX + padding, textY, "Hide finished books", true, EpdFontFamily::REGULAR);
    Toggle::render(renderer, panelX + panelWidth - padding, rowY, rowHeight,
                   SETTINGS.hideFinishedBooks != 0);
  }
}

void Library::handleFilterTap(const int tapX, const int tapY) {
  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  const int panelWidth = std::min(screenWidth - 48, 330);
  constexpr int panelHeight = 370;
  const int panelX = (screenWidth - panelWidth) / 2;
  const int panelY = std::max(navigation::Menu::height + 20, (screenHeight - panelHeight) / 2);
  constexpr int padding = 18;
  constexpr int titleHeight = 28;
  constexpr int tabContentGap = 20;
  constexpr int gap = 10;
  constexpr int allRowGap = 10;
  constexpr int allBottomMargin = 20;
  const int gridY = panelY + padding + titleHeight + tabContentGap;
  const int cellWidth = (panelWidth - padding * 2 - gap * 2) / 3;
  const int cellHeight =
      (panelHeight - padding * 2 - titleHeight - tabContentGap - gap * 2 - allRowGap - allBottomMargin) / 4;
  const int selectorSize = std::min(cellWidth, cellHeight);

  // Title | Type | Options header - same equal-width columns filterPopup() draws the labels into.
  if (tapY >= panelY && tapY < gridY) {
    const int tabAreaX = panelX + padding;
    const int tabAreaWidth = panelWidth - padding * 2;
    const int tabColumnWidth = tabAreaWidth / 3;
    const int tab = std::clamp((tapX - tabAreaX) / std::max(1, tabColumnWidth), 0, 2);
    if (tapX >= panelX && tapX < panelX + panelWidth) {
      filterTab = static_cast<FilterTab>(tab);
      updateRequired = true;
      return;
    }
  }

  if (filterTab == FilterTab::Title) {
    const int allY = gridY + 3 * (cellHeight + gap) + allRowGap;
    const int allX = panelX + (panelWidth - selectorSize) / 2;
    const int leftX = panelX + padding;
    const int rightX = panelX + panelWidth - padding - selectorSize;

    if (tapY >= allY && tapY < allY + selectorSize) {
      if (tapX >= leftX && tapX < leftX + selectorSize) {
        changeFilterPage(-1);
        return;
      }
      if (tapX >= rightX && tapX < rightX + selectorSize) {
        changeFilterPage(1);
        return;
      }
      if (tapX >= allX && tapX < allX + selectorSize) {
        applyFilter(9);
        return;
      }
    }

    if (tapX >= panelX + padding && tapX < panelX + panelWidth - padding && tapY >= gridY && tapY < allY) {
      const int column = (tapX - panelX - padding) / (cellWidth + gap);
      const int row = (tapY - gridY) / (cellHeight + gap);
      if (column >= 0 && column < 3 && row >= 0 && row < 3) {
        const int cellX = panelX + padding + column * (cellWidth + gap);
        const int cellY = gridY + row * (cellHeight + gap);
        if (tapX < cellX + cellWidth && tapY < cellY + cellHeight) {
          applyFilter(row * 3 + column);
          return;
        }
      }
    }
  } else if (filterTab == FilterTab::Type) {
    // Same cell grid hit-test as the Title tab above, just capped to 5 real options instead of 9.
    constexpr int typeOptionCount = 5;
    const int gridBottom = gridY + 2 * (cellHeight + gap);
    if (tapX >= panelX + padding && tapX < panelX + panelWidth - padding && tapY >= gridY && tapY < gridBottom) {
      const int column = (tapX - panelX - padding) / (cellWidth + gap);
      const int row = (tapY - gridY) / (cellHeight + gap);
      if (column >= 0 && column < 3 && row >= 0 && row < 2) {
        const int cellX = panelX + padding + column * (cellWidth + gap);
        const int cellY = gridY + row * (cellHeight + gap);
        const int index = row * 3 + column;
        if (tapX < cellX + cellWidth && tapY < cellY + cellHeight && index < typeOptionCount) {
          applyTypeFilter(index);
          return;
        }
      }
    }
  } else {
    const int rowY = gridY;
    if (tapX >= panelX + padding && tapX < panelX + panelWidth - padding && tapY >= rowY &&
        tapY < rowY + cellHeight) {
      SETTINGS.hideFinishedBooks = SETTINGS.hideFinishedBooks == 0 ? 1 : 0;
      SETTINGS.saveToFile();
      load();
      updateRequired = true;
      return;
    }
  }

  filterOpen = false;
  updateRequired = true;
}
