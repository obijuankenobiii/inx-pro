#include "Library.h"
#include "system/UiLayout.h"

#include <ArduinoJson.h>
#include <BitmapRender.h>
#include <GfxRenderer.h>
#include <SDCardManager.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <functional>
#include <map>
#include <string>
#include <utility>

#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "components/global/PopUp.h"
#include "components/global/Button.h"
#include "components/global/Sidebar.h"
#include "components/global/Toggle.h"
#include "components/library/AllBooksLibrary.h"
#include "images/Filter.h"
#include "images/Hamburger.h"
#include "images/LibraryFilterLeft.h"
#include "images/LibraryFilterRight.h"
#include "images/LibraryViewGrid.h"
#include "images/LibraryViewList.h"
#include "images/LibraryViewThumb.h"
#include "images/Refresh.h"
#include "images/Search.h"
#include "images/SortAsc.h"
#include "images/SortDesc.h"
#include "state/BookState.h"
#include "state/EpubNotesIndex.h"
#include "state/RecentBooks.h"
#include "state/Session.h"
#include "state/SystemSetting.h"
#include "util/MetadataIndex.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "system/PluginManager.h"
#include "system/ScreenComponents.h"
#include "util/LibraryIndexRefresh.h"
#include "util/SdIoMutex.h"

extern void onGoToLibrary(const std::string& path);

namespace {
std::map<std::string, int> gThumbPage;

constexpr int kFilterCategoryCount = 5;
constexpr int kFilterMenuRowCount = kFilterCategoryCount + 1;
constexpr int kRefreshIconSize = 40;
constexpr int kCaretSourceWidth = 32;
constexpr int kCaretSourceHeight = 30;
constexpr int kCaretIconSize = 30;
constexpr int kControlListItemHeight = UiLayout::LIST_ITEM_HEIGHT + 10;

struct FilterDrawerLayout {
  int x;
  int y;
  int width;
  int height;
  int headerHeight;
  int doneHeight;
  int rowHeight;
  int visibleRows;
};

struct FilterFooterLayout {
  ButtonBounds back;
  ButtonBounds clear;
  ButtonBounds done;
  int caretSize;
  bool hasBack;
};

struct ControlModalLayout {
  int x;
  int y;
  int width;
  int height;
  int tabY;
  int tabHeight;
  int contentY;
};

ControlModalLayout controlModalLayout(const GfxRenderer& renderer) {
  constexpr int horizontalScreenMargin = 20;
  constexpr int bottomMargin = 20;
  const int screenHeight = renderer.getScreenHeight();
  const int screenWidth = renderer.getScreenWidth();
  const int x = horizontalScreenMargin;
  const int width = std::max(1, screenWidth - horizontalScreenMargin * 2);
  constexpr int tabPaddingHeight = 20 + 20;
  constexpr int sortRowCount = 6;
  const int tabHeight = tabPaddingHeight + renderer.text.getLineHeight(systemFontId());
  const int contentHeight = tabHeight + sortRowCount * kControlListItemHeight;
  const int y = std::max(navigation::Menu::height + 10, screenHeight - bottomMargin - contentHeight);
  const int height = std::max(1, screenHeight - bottomMargin - y);
  const int tabY = y;
  const int contentY = tabY + tabHeight;
  return {x, y, width, height, tabY, tabHeight, contentY};
}

FilterDrawerLayout filterDrawerLayout(const GfxRenderer& renderer, const int rowCount) {
  constexpr int maxVisibleRows = 6;
  const ControlModalLayout modal = controlModalLayout(renderer);
  constexpr int headerHeight = 0;
  const int doneHeight = kControlListItemHeight;
  const int rowHeight = kControlListItemHeight;
  const int y = modal.contentY;
  const int availableRows = std::max(1, (modal.y + modal.height - y - headerHeight - doneHeight) / rowHeight);
  const int visibleRows = std::min({std::max(1, rowCount), availableRows, maxVisibleRows});
  const int height = std::max(headerHeight + visibleRows * rowHeight + doneHeight, modal.y + modal.height - y);
  return {modal.x, y, modal.width, height, headerHeight, doneHeight, rowHeight, visibleRows};
}

void renderFilterScrollbar(const GfxRenderer& renderer, const FilterDrawerLayout& drawer, const int totalRows,
                           const int visibleRows, const int scrollOffset) {
  if (totalRows <= visibleRows || visibleRows <= 0) return;
  constexpr int barWidth = 2;
  const int trackY = drawer.y + drawer.headerHeight;
  const int trackHeight = visibleRows * drawer.rowHeight;
  const int thumbHeight = std::max(12, trackHeight * visibleRows / totalRows);
  const int maxOffset = totalRows - visibleRows;
  const int thumbY = trackY + (trackHeight - thumbHeight) * scrollOffset / maxOffset;
  const int barX = drawer.x + drawer.width - 5;
  renderer.rectangle.fill(barX, trackY, barWidth, trackHeight,
                          static_cast<int>(GfxRenderer::FillTone::Gray), true);
  renderer.rectangle.fill(barX, thumbY, barWidth, thumbHeight,
                          static_cast<int>(GfxRenderer::FillTone::Ink), true);
}

FilterFooterLayout filterFooterLayout(const GfxRenderer& renderer, const FilterDrawerLayout& drawer,
                                      const bool hasBack, const int font) {
  constexpr int backLeftMargin = 20;
  constexpr int backButtonGap = 8;
  constexpr int backTouchWidth = 48;
  const int caretSize = 28;
  const int buttonSpace = std::max(1, drawer.width -
                                         (hasBack ? backLeftMargin + backTouchWidth + backButtonGap : 0));
  const int maxButtonWidth = std::max(1, buttonSpace / 2);
  const int doneWidth = std::min(Button::width(renderer, "Done", font), maxButtonWidth);
  const int clearWidth = std::min(Button::width(renderer, "Clear", font), maxButtonWidth);
  const int footerY = drawer.y + drawer.height - drawer.doneHeight;
  const int doneX = drawer.x + drawer.width - doneWidth;
  const int clearX = doneX - clearWidth;
  const ButtonBounds back{drawer.x + backLeftMargin, footerY, backTouchWidth, drawer.doneHeight};
  const ButtonBounds clear{clearX, footerY, clearWidth, drawer.doneHeight};
  const ButtonBounds done{doneX, footerY, doneWidth, drawer.doneHeight};
  return {back, clear, done, caretSize, hasBack};
}

void renderControlTab(const GfxRenderer& renderer, const ControlModalLayout& modal, const int index,
                      const char* label, const uint8_t* icon, const bool selected) {
  const int font = systemFontId();
  const EpdFontFamily::Style tabFont = EpdFontFamily::REGULAR;
  const int x = modal.x + modal.width * index / 3;
  const int nextX = modal.x + modal.width * (index + 1) / 3;
  const int width = nextX - x;
  const int textWidth = renderer.text.getWidth(font, label, tabFont);
  constexpr int iconSize = 24;
  constexpr int iconGap = 11;
  const int contentWidth = iconSize + iconGap + textWidth;
  const int tabCenterY = modal.tabY + modal.tabHeight / 2;
  const int textY = tabCenterY - renderer.text.getLineHeight(font) / 2;
  const int contentX = x + (width - contentWidth) / 2;
  const int iconY = tabCenterY - iconSize / 2;
  renderer.bitmap.iconScaled(icon, contentX, iconY, 40, 40, iconSize, iconSize);
  const int textX = contentX + iconSize + iconGap;
  renderer.text.render(font, textX, textY, label, true, tabFont);
  if (selected) {
    const int dividerY = modal.tabY + modal.tabHeight;
    renderer.rectangle.fill(contentX, dividerY - 1, std::max(1, contentWidth), 2, true);
  }
}
}
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

const LibraryIndex::Book* singleBookInFolder(const std::string& folder,
                                             const std::vector<LibraryIndex::Book>& books) {
  const std::string prefix = folder == "/" ? "/" : folder + "/";
  const LibraryIndex::Book* result = nullptr;
  for (const LibraryIndex::Book& book : books) {
    if (book.type != LibraryIndex::Book::Type::BOOK || book.path.compare(0, prefix.size(), prefix) != 0) {
      continue;
    }
    if (result != nullptr) return nullptr;
    result = &book;
  }
  return result;
}

std::string lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

std::string formatSeriesPosition(std::string value) {
  const size_t decimal = value.find('.');
  if (decimal == std::string::npos) return value;
  while (value.size() > decimal + 1 && value.back() == '0') value.pop_back();
  if (value.size() == decimal + 1) value.pop_back();
  return value;
}

// Compare labels the way readers expect: "Volume 2" comes before
// "Volume 10".  Numeric runs are compared by value while the surrounding
// text remains case-insensitive.
int naturalCompare(const std::string& left, const std::string& right) {
  size_t leftPos = 0;
  size_t rightPos = 0;
  while (leftPos < left.size() && rightPos < right.size()) {
    const unsigned char leftChar = static_cast<unsigned char>(left[leftPos]);
    const unsigned char rightChar = static_cast<unsigned char>(right[rightPos]);
    if (std::isdigit(leftChar) && std::isdigit(rightChar)) {
      const size_t leftRunStart = leftPos;
      const size_t rightRunStart = rightPos;
      while (leftPos < left.size() &&
             std::isdigit(static_cast<unsigned char>(left[leftPos]))) {
        ++leftPos;
      }
      while (rightPos < right.size() &&
             std::isdigit(static_cast<unsigned char>(right[rightPos]))) {
        ++rightPos;
      }

      size_t leftSignificant = leftRunStart;
      size_t rightSignificant = rightRunStart;
      while (leftSignificant + 1 < leftPos && left[leftSignificant] == '0') ++leftSignificant;
      while (rightSignificant + 1 < rightPos && right[rightSignificant] == '0') ++rightSignificant;

      const size_t leftDigits = leftPos - leftSignificant;
      const size_t rightDigits = rightPos - rightSignificant;
      if (leftDigits != rightDigits) return leftDigits < rightDigits ? -1 : 1;

      const int digitsComparison = left.compare(leftSignificant, leftDigits,
                                                right, rightSignificant, rightDigits);
      if (digitsComparison != 0) return digitsComparison < 0 ? -1 : 1;

      // Equal numeric values: prefer the spelling with fewer leading zeroes.
      const size_t leftRunLength = leftPos - leftRunStart;
      const size_t rightRunLength = rightPos - rightRunStart;
      if (leftRunLength != rightRunLength) return leftRunLength < rightRunLength ? -1 : 1;
      continue;
    }

    const char leftLower = static_cast<char>(std::tolower(leftChar));
    const char rightLower = static_cast<char>(std::tolower(rightChar));
    if (leftLower != rightLower) return leftLower < rightLower ? -1 : 1;
    ++leftPos;
    ++rightPos;
  }

  if (leftPos == left.size() && rightPos == right.size()) return 0;
  return leftPos == left.size() ? -1 : 1;
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

bool isDeletableFolder(const LibraryIndex::Book& item) {
  if (item.type != LibraryIndex::Book::Type::FOLDER || item.path.empty() || item.path == "/") return false;
  return item.path.compare(0, std::string("/.metadata/").size(), "/.metadata/") != 0;
}

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

}

Library::Library(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string path,
                 PluginManager::LibraryMenuLink pluginMenu)
    : UiPage("Library", renderer, mappedInput),
      path(cleanPath(std::move(path))),
      pluginMenu_(std::move(pluginMenu)),
      pluginMode_(!pluginMenu_.id.empty()),
      grid(renderer, mappedInput, items, [this](const int index, const bool longPress) { select(index, longPress); },
           [this](const LibraryIndex::Book& book) { return isFavorite(book); },
           [this](const int x, const int y) { routeMenuAction(navigation::Menu::handleTap(x, y)); }),
      list(renderer, mappedInput, items, [this](const int index, const bool longPress) { select(index, longPress); },
           [this](const LibraryIndex::Book& book) { return isFavorite(book); },
           [this](const int x, const int y) { routeMenuAction(navigation::Menu::handleTap(x, y)); }),
      listThumb(renderer, mappedInput, items, books,
                [this](const int index, const bool longPress) { select(index, longPress); },
                [this](const LibraryIndex::Book& book) { return isFavorite(book); },
                [this](const int x, const int y) { routeMenuAction(navigation::Menu::handleTap(x, y)); },
                [this](const LibraryIndex::Book& book) {
                  const std::vector<std::string> covers = groupCovers(book, 1);
                  return covers.empty() ? std::string() : covers.front();
                },
                [this] { return stateFilter != StateFilter::None; }),
      thumb(renderer, mappedInput, items, books,
            [this](const int index, const bool longPress) { select(index, longPress); },
            [this](const LibraryIndex::Book& book) { return isFavorite(book); },
            [this](const int x, const int y) { routeMenuAction(navigation::Menu::handleTap(x, y)); },
            [this](const LibraryIndex::Book& group, const int limit) { return groupCovers(group, limit); }) {}

void Library::onEnter() {
  UiPage::onEnter();
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
    case SystemSetting::LIBRARY_LIST_THUMBNAIL:
      view = View::ListThumb;
      break;
    default:
      view = View::Grid;
      SETTINGS.libraryMode = SystemSetting::LIBRARY_GRID;
      SETTINGS.saveToFile();
      break;
  }
  sort = static_cast<Sort>(std::min<int>(SETTINGS.librarySortMode, sortCount() - 1));
  resetFilterSelections();
  filterCategory_ = FilterCategory::Categories;
  filterMetadataGroups_.clear();
  filterMetadataIndexAvailable_ = false;
  filterScrollOffset_ = 0;
  sortOpen = false;
  filterOpen = false;
  controlModalOpen = false;
  controlTab = ControlTab::View;
  listThumbDeferImageDecode_ = view == View::ListThumb;
  listThumbWarmupPending_ = false;
  popupBook = -1;
  folderDeleteConfirm = false;
  sidebarOpen = false;
  stateFilter = StateFilter::None;
  activePluginGroup_.clear();
  metadataGroupKey_.clear();
  metadataIndexAvailable_ = false;
  sidebarScrollOffset_ = 0;
  allBooksMode = false;
  // Defer the expensive first root load only for thumbnail mode so its shell
  // can draw immediately. List/grid and folder pages should open synchronously.
  loading = path == "/" && view == View::Thumb;
  items.clear();
  books.clear();
  resetViews();

  if (path == "/" && !pluginMode_ && SETTINGS.libraryViewMode == SystemSetting::LIBRARY_VIEW_PLUGIN) {
    PluginManager::LibraryMenuLink savedPluginMenu;
    if (PluginManager::findLibraryMenuPlugin(savedPluginMenu)) {
      pluginMenu_ = std::move(savedPluginMenu);
      pluginMode_ = true;
    } else {
      SETTINGS.libraryViewMode = SystemSetting::LIBRARY_VIEW_FOLDERS;
      SETTINGS.saveToFile();
    }
  }

  if (pluginMode_) {
    stateFilter = StateFilter::Plugin;
    if (SETTINGS.libraryViewMode != SystemSetting::LIBRARY_VIEW_PLUGIN) {
      SETTINGS.libraryViewMode = SystemSetting::LIBRARY_VIEW_PLUGIN;
      SETTINGS.saveToFile();
    }
  } else if (path != "/") {
    if (SETTINGS.libraryViewMode != SystemSetting::LIBRARY_VIEW_FOLDERS) {
      SETTINGS.libraryViewMode = SystemSetting::LIBRARY_VIEW_FOLDERS;
      SETTINGS.saveToFile();
    }
  } else {
    switch (SETTINGS.libraryViewMode) {
      case SystemSetting::LIBRARY_VIEW_BOOKS:
        allBooksMode = true;
        break;
      case SystemSetting::LIBRARY_VIEW_FAVORITES:
        stateFilter = StateFilter::Favorites;
        break;
      case SystemSetting::LIBRARY_VIEW_READING:
        stateFilter = StateFilter::Reading;
        break;
      case SystemSetting::LIBRARY_VIEW_FINISHED:
        stateFilter = StateFilter::Finished;
        break;
      case SystemSetting::LIBRARY_VIEW_AUTHORS:
        stateFilter = StateFilter::Metadata;
        metadataKind_ = MetadataIndex::Kind::Authors;
        break;
      case SystemSetting::LIBRARY_VIEW_TAGS:
        stateFilter = StateFilter::Metadata;
        metadataKind_ = MetadataIndex::Kind::Tags;
        break;
      case SystemSetting::LIBRARY_VIEW_SERIES:
        stateFilter = StateFilter::Metadata;
        metadataKind_ = MetadataIndex::Kind::Series;
        break;
      default:
        if (SETTINGS.libraryViewMode != SystemSetting::LIBRARY_VIEW_FOLDERS) {
          SETTINGS.libraryViewMode = SystemSetting::LIBRARY_VIEW_FOLDERS;
          SETTINGS.saveToFile();
        }
        break;
    }
  }
  if (!loading) load();
}

void Library::load() {
  items.clear();
  resetViews();
  books.clear();
  pluginGroupByPath_.clear();
  pluginOrderByPath_.clear();
  pluginBooksByGroup_.clear();
  pluginNameByGroup_.clear();
  metadataKeyByGroup_.clear();
  favorites.clear();
  metadataIndexAvailable_ = false;
  for (const BookState::Book& book : BOOK_STATE.getFavoriteBooks()) {
    favorites.insert(book.path);
  }
  const bool hasMetadataFilter = std::any_of(
      metadataFilters_.begin(), metadataFilters_.end(),
      [](const MetadataFilterSelection& selection) { return !selection.selectedKeys.empty(); });
  if (stateFilter == StateFilter::Plugin) {
    std::string output;
    std::string error;
    if (!PluginManager::invokeString(pluginMenu_.id.c_str(), pluginMenu_.function.c_str(), nullptr, output, error)) return;

    JsonDocument document;
    if (deserializeJson(document, output) != DeserializationError::Ok || !document.is<JsonArray>()) return;
    std::unordered_map<std::string, std::vector<LibraryIndex::Book>> groupedBooks;
    for (JsonObject record : document.as<JsonArray>()) {
      const char* pathValue = record["path"] | "";
      if (!pathValue || !pathValue[0]) continue;
      const std::string bookPath = cleanPath(pathValue);
      LibraryIndex::Book item;
      item.type = LibraryIndex::Book::Type::BOOK;
      item.path = bookPath;
      item.folder = parent(bookPath);
      const char* titleValue = record["title"] | "";
      const char* authorValue = record["author"] | "";
      if (titleValue && titleValue[0]) item.title = titleValue;
      if (authorValue && authorValue[0]) item.author = authorValue;

      const char* groupValue = record[pluginMenu_.groupField.c_str()] | "";
      const std::string groupName = groupValue ? groupValue : "";
      if (groupName.empty()) continue;
      pluginGroupByPath_[bookPath] = groupName;
      pluginOrderByPath_[bookPath] = record[pluginMenu_.orderField.c_str()] | 0.0f;
      groupedBooks[groupName].push_back(std::move(item));
    }

    for (auto& group : groupedBooks) {
      const int groupSize = static_cast<int>(group.second.size());
      for (size_t index = 0; index < group.second.size(); ++index) {
        LibraryIndex::Book& book = group.second[index];
        const auto order = pluginOrderByPath_.find(cleanPath(book.path));
        const int position = order != pluginOrderByPath_.end() && order->second > 0
                                 ? order->second
                                 : static_cast<int>(index) + 1;
        book.badge = "#" + std::to_string(position) + "/" + std::to_string(groupSize);
      }
      const std::string groupPath = "/.metadata/plugin-groups/" + pluginMenu_.id + "/" +
                                    std::to_string(std::hash<std::string>{}(group.first));
      pluginBooksByGroup_[groupPath] = group.second;
      pluginNameByGroup_[groupPath] = group.first;
      if (activePluginGroup_.empty()) {
        LibraryIndex::Book folder;
        folder.type = LibraryIndex::Book::Type::FOLDER;
        folder.path = groupPath;
        folder.title = group.first;
        folder.folder = "/";
        folder.bookCount = static_cast<uint16_t>(std::min<size_t>(group.second.size(), 65535));
        folder.hasMetadata = true;
        items.push_back(std::move(folder));
      } else if (activePluginGroup_ == groupPath) {
        items = group.second;
      }
    }
    books.clear();
  } else if (stateFilter == StateFilter::Metadata) {
    std::vector<MetadataIndex::Group> groups;
    if (metadataGroupKey_.empty()) {
      metadataIndexAvailable_ = MetadataIndex::loadGroups(metadataKind_, groups);
    } else {
      MetadataIndex::Group group;
      metadataIndexAvailable_ = MetadataIndex::findGroup(metadataKind_, metadataGroupKey_, group);
      if (metadataIndexAvailable_) groups.push_back(std::move(group));
    }
    if (!metadataIndexAvailable_) return;

    if (metadataGroupKey_.empty()) {
      items.reserve(groups.size());
      const char* kindName = metadataKind_ == MetadataIndex::Kind::Authors ? "authors" :
                             metadataKind_ == MetadataIndex::Kind::Tags ? "tags" : "series";
      for (const MetadataIndex::Group& group : groups) {
        LibraryIndex::Book folder;
        folder.type = LibraryIndex::Book::Type::FOLDER;
        folder.path = std::string("/.metadata/library/") + kindName + "/" +
                      std::to_string(std::hash<std::string>{}(group.key));
        folder.title = group.label;
        folder.folder = "/";
        folder.bookCount = static_cast<uint16_t>(std::min<uint32_t>(group.count, 65535));
        folder.hasMetadata = true;
        metadataKeyByGroup_[folder.path] = group.key;
        items.push_back(std::move(folder));
      }
    } else {
      for (const MetadataIndex::Group& group : groups) {
        std::vector<MetadataIndex::Entry> entries;
        if (!MetadataIndex::loadGroup(metadataKind_, group, entries)) return;
        items.reserve(entries.size());
        for (size_t i = 0; i < entries.size(); ++i) {
          MetadataIndex::Entry& entry = entries[i];
          LibraryIndex::Book book;
          book.type = LibraryIndex::Book::Type::BOOK;
          book.path = cleanPath(std::move(entry.path));
          book.title = std::move(entry.title);
          book.author = std::move(entry.author);
          book.folder = parent(book.path);
          if (metadataKind_ == MetadataIndex::Kind::Series) {
            const std::string position = entry.order.empty() ? std::to_string(i + 1)
                                                             : formatSeriesPosition(entry.order);
            book.badge = "#" + position + "/" + std::to_string(group.count);
          }
          items.push_back(std::move(book));
        }
      }
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
  } else if (hasMetadataFilter) {
    // Metadata filters are global library filters, not scoped to the folder
    // currently open. Their indexes contain book paths from the whole library.
    if (!LibraryIndex::search("", items, LibraryIndex::all)) return;
    items.erase(std::remove_if(items.begin(), items.end(), [](const LibraryIndex::Book& item) {
                  return item.type != LibraryIndex::Book::Type::BOOK;
                }),
                items.end());
    books.clear();
  } else if (allBooksMode) {
    if (!AllBooksLibrary::load(items)) return;
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

  if (titleFilters_.any()) {
    items.erase(std::remove_if(items.begin(), items.end(), [this](const LibraryIndex::Book& item) {
                  const char initial = firstLetter(item.title);
                  return initial < 'A' || initial > 'Z' || !titleFilters_.test(initial - 'A');
                }),
                items.end());
  }

  for (const MetadataFilterSelection& selection : metadataFilters_) {
    if (selection.selectedKeys.empty()) continue;
    items.erase(std::remove_if(items.begin(), items.end(), [&selection](const LibraryIndex::Book& item) {
                  if (item.type != LibraryIndex::Book::Type::BOOK) return true;
                  const std::string path = cleanPath(item.path);
                  return selection.pathMatchCounts.find(std::string_view(path)) == selection.pathMatchCounts.end();
                }),
                items.end());
  }

  if (typeFilters_.any()) {
    items.erase(std::remove_if(items.begin(), items.end(), [this](const LibraryIndex::Book& item) {
                  if (item.type != LibraryIndex::Book::Type::BOOK) return false;
                  for (int index = 0; index < 4; ++index) {
                    if (typeFilters_.test(index) && matchesTypeFilter(item.path, typeFilterCategory(index))) {
                      return false;
                    }
                  }
                  return true;
                }),
                items.end());
  }

  if (std::none_of(items.begin(), items.end(), [](const LibraryIndex::Book& item) {
        return item.type == LibraryIndex::Book::Type::FOLDER;
      })) {
    std::vector<LibraryIndex::Book>().swap(books);
  }

  if (stateFilter != StateFilter::Metadata) {
    std::stable_sort(items.begin(), items.end(), [this](const LibraryIndex::Book& left,
                                                         const LibraryIndex::Book& right) {
    if (stateFilter == StateFilter::Plugin) {
      const std::string leftPath = cleanPath(left.path);
      const std::string rightPath = cleanPath(right.path);
      const auto leftGroup = pluginGroupByPath_.find(leftPath);
      const auto rightGroup = pluginGroupByPath_.find(rightPath);
      const std::string leftName = leftGroup == pluginGroupByPath_.end() ? "" : leftGroup->second;
      const std::string rightName = rightGroup == pluginGroupByPath_.end() ? "" : rightGroup->second;
      if (leftName != rightName) return leftName < rightName;
      const auto leftOrder = pluginOrderByPath_.find(leftPath);
      const auto rightOrder = pluginOrderByPath_.find(rightPath);
        const float leftValue = leftOrder == pluginOrderByPath_.end() ? 0.0f : leftOrder->second;
        const float rightValue = rightOrder == pluginOrderByPath_.end() ? 0.0f : rightOrder->second;
      if (leftValue != rightValue) return leftValue < rightValue;
    }
    const std::string leftTitle = lower(left.title);
    const std::string rightTitle = lower(right.title);
    const std::string leftFolder = lower(left.folder.empty() ? parent(left.path) : left.folder);
    const std::string rightFolder = lower(right.folder.empty() ? parent(right.path) : right.folder);
    if (sort == Sort::AuthorAZ || sort == Sort::AuthorZA) {
      const std::string leftAuthor = lower(left.author);
      const std::string rightAuthor = lower(right.author);
      const int authorComparison = naturalCompare(leftAuthor, rightAuthor);
      if (authorComparison != 0) {
        return sort == Sort::AuthorAZ ? authorComparison < 0 : authorComparison > 0;
      }
    }
    if (sort == Sort::FolderAZ || sort == Sort::FolderZA) {
      const int folderComparison = naturalCompare(leftFolder, rightFolder);
      if (folderComparison != 0) {
        return sort == Sort::FolderAZ ? folderComparison < 0 : folderComparison > 0;
      }
    }
    const int titleComparison = naturalCompare(leftTitle, rightTitle);
    if (sort == Sort::TitleZA || sort == Sort::FolderZA || sort == Sort::AuthorZA) {
      return titleComparison > 0;
    }
    return titleComparison < 0;
    });
  }
  resetViews();
  thumb.setRoot(path == "/" && !allBooksMode);
  restoreThumbPage();
  if (view == View::Thumb) thumb.load();
}

void Library::open(const int index) {
  if (index < 0 || index >= static_cast<int>(items.size())) return;
  const LibraryIndex::Book& item = items[static_cast<size_t>(index)];
  if (item.type == LibraryIndex::Book::Type::FOLDER) {
    if (stateFilter == StateFilter::Plugin && activePluginGroup_.empty() &&
        (pluginBooksByGroup_.find(item.path) != pluginBooksByGroup_.end() ||
         pluginNameByGroup_.find(item.path) != pluginNameByGroup_.end())) {
      activePluginGroup_ = item.path;
      thumb.setPage(0);
      load();
      updateRequired = true;
      return;
    }
    if (stateFilter == StateFilter::Metadata && metadataGroupKey_.empty()) {
      const auto group = metadataKeyByGroup_.find(item.path);
      if (group == metadataKeyByGroup_.end()) return;
      metadataGroupKey_ = group->second;
      thumb.setPage(0);
      load();
      updateRequired = true;
      return;
    }
    gThumbPage[path] = thumb.currentPage();
    onGoToLibrary(item.path);
  } else {
    const std::string libraryPath = path;
    openReaderFromCallback(item.path, [libraryPath] { onGoToLibrary(libraryPath); });
  }
}

void Library::loop() {
  // Shared navigation must see the edge swipe before library navigation treats
  // it as a folder/back gesture. This keeps the hidden menu available on the
  // Library main page and inside library folders.
  if (controlModalOpen) {
    if (controlModalInput()) return;
    renderPage();
    return;
  }
  if (menuInput()) return;

  if (loading) {
    if (updateRequired) {
      renderPage();
      return;
    }
    loading = false;
    load();
    updateRequired = true;
    return;
  }

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

  if (!isOpen() && horizontalSwipe) {
    if (stateFilter == StateFilter::Plugin && !activePluginGroup_.empty() && mappedInput.wasTouchSwipeRight()) {
      activePluginGroup_.clear();
      thumb.setPage(0);
      load();
      updateRequired = true;
      return;
    }
    if (stateFilter == StateFilter::Metadata && !metadataGroupKey_.empty() && mappedInput.wasTouchSwipeRight()) {
      metadataGroupKey_.clear();
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

  if (isOpen()) {
    renderPage();
    return;
  }

  // The first thumbnail-list frame intentionally uses placeholders. Let it
  // reach the panel before decoding covers, just like Home's widget layout.
  if (listThumbWarmupPending_ && !mappedInput.wasTouchActivity()) {
    listThumbWarmupPending_ = false;
    updateRequired = true;
    renderPage();
  }

  bool handled = false;
  switch (view) {
    case View::List:
      handled = list.handleInput();
      break;
    case View::Grid:
      handled = grid.handleInput();
      break;
    case View::ListThumb:
      handled = listThumb.handleInput();
      break;
    case View::Thumb:
      handled = thumb.handleInput();
      break;
  }
  if (handled) {
    if (view == View::ListThumb) {
      // A page change should get the same deferred first render as the
      // initial list. This also keeps a newly loaded page responsive.
      listThumbDeferImageDecode_ = true;
      listThumbWarmupPending_ = false;
    }
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
    thumb.prefetchNextPage();
  }
  renderPage();
}

void Library::content() {
  if (loading && view == View::Thumb) {
    renderer.text.centered(systemFontId(), renderer.getScreenHeight() / 2, "Loading library...");
    return;
  }
  if (items.empty()) {
    const char* message = stateFilter == StateFilter::Plugin
                              ? "No items in this plugin view"
                              : (stateFilter == StateFilter::Metadata && !metadataIndexAvailable_
                                     ? "Generate Metadata in Settings first"
                                     : (stateFilter == StateFilter::Metadata ? "No metadata groups found"
                                                                            : (LibraryIndex::hasIndex()
                                                                                   ? "No books in this folder"
                                                                                   : "Build the library index first")));
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
    case View::ListThumb:
      renderer.setDeferImageDecode(listThumbDeferImageDecode_);
      listThumb.render();
      renderer.setDeferImageDecode(false);
      if (listThumbDeferImageDecode_) {
        listThumbDeferImageDecode_ = false;
        listThumbWarmupPending_ = true;
      }
      break;
    case View::Thumb:
      thumb.render();
      break;
  }
}

std::vector<std::string> Library::groupCovers(const LibraryIndex::Book& group, const int limit) const {
  std::vector<std::string> covers;
  if (group.type != LibraryIndex::Book::Type::FOLDER || limit <= 0) {
    return covers;
  }

  if (stateFilter == StateFilter::Plugin) {
    const auto booksForGroup = pluginBooksByGroup_.find(group.path);
    if (booksForGroup == pluginBooksByGroup_.end()) return covers;
    const char* names[] = {"cover.jpg", "thumb.jpg", "cover.bmp", "thumb.png", "thumb.bmp"};
    for (const LibraryIndex::Book& book : booksForGroup->second) {
      const std::string directory = dataPath(book.path);
      for (const char* name : names) {
        const std::string coverPath = directory + "/" + name;
        if (!SdMan.exists(coverPath.c_str())) continue;
        covers.push_back(coverPath);
        break;
      }
      if (static_cast<int>(covers.size()) >= limit) break;
    }
    return covers;
  }

  if (stateFilter != StateFilter::Metadata ||
      (metadataKind_ != MetadataIndex::Kind::Authors && metadataKind_ != MetadataIndex::Kind::Series &&
       metadataKind_ != MetadataIndex::Kind::Tags)) {
    return covers;
  }
  const auto metadataKey = metadataKeyByGroup_.find(group.path);
  if (metadataKey == metadataKeyByGroup_.end()) return covers;

  MetadataIndex::Group groupIndex;
  if (!MetadataIndex::findGroup(metadataKind_, metadataKey->second, groupIndex)) return covers;
  std::vector<MetadataIndex::Entry> entries;
  if (!MetadataIndex::loadGroup(metadataKind_, groupIndex, entries, 64)) return covers;

  const char* names[] = {"thumb.jpg", "thumb.png", "thumb.bmp"};
  for (const MetadataIndex::Entry& entry : entries) {
    const std::string directory = dataPath(entry.path);
    for (const char* name : names) {
      const std::string thumbnailPath = directory + "/" + name;
      if (!SdMan.exists(thumbnailPath.c_str())) continue;
      covers.push_back(thumbnailPath);
      break;
    }
    if (static_cast<int>(covers.size()) >= limit) break;
  }
  return covers;
}

void Library::title() const {
  renderer.bitmap.icon(Hamburger, navigation::Menu::leftMargin, navigation::Menu::topPadding,
                       navigation::Menu::iconSize, navigation::Menu::iconSize);

  const char* heading = "Library";
  switch (stateFilter) {
    case StateFilter::Favorites:
      heading = "Favorites";
      break;
    case StateFilter::Reading:
      heading = "Reading";
      break;
    case StateFilter::Finished:
      heading = "Finished";
      break;
    case StateFilter::Metadata:
      heading = metadataKind_ == MetadataIndex::Kind::Authors
                    ? "Authors"
                    : (metadataKind_ == MetadataIndex::Kind::Tags ? "Tags" : "Series");
      break;
    case StateFilter::Plugin:
      heading = pluginMenu_.label.empty() ? "Library" : pluginMenu_.label.c_str();
      break;
    case StateFilter::None:
      if (allBooksMode) heading = "All books";
      break;
  }

  const int font = systemFontIdForPointSize(16);
  const int textY = navigation::Menu::topPadding +
                    (navigation::Menu::iconSize - renderer.text.getLineHeight(font)) / 2;
  const int textX = navigation::Menu::leftMargin + navigation::Menu::iconSize + 12;
  const int maxTextWidth = std::max(1, renderer.getScreenWidth() - 80 - textX);
  const std::string shown = renderer.text.truncate(font, heading, maxTextWidth, EpdFontFamily::BOLD);
  renderer.text.render(font, textX, textY, shown.c_str(), true, EpdFontFamily::BOLD);
}

void Library::select(const int index, const bool longPress) {
  if (index < 0 || index >= static_cast<int>(items.size())) return;
  if (longPress && (items[static_cast<size_t>(index)].type == LibraryIndex::Book::Type::BOOK ||
                    isDeletableFolder(items[static_cast<size_t>(index)]))) {
    popupBook = index;
    folderDeleteConfirm = false;
    sortOpen = false;
    filterOpen = false;
    updateRequired = true;
    return;
  }
  open(index);
}

void Library::popup() const {
  const LibraryIndex::Book& book = items[static_cast<size_t>(popupBook)];
  if (folderDeleteConfirm) {
    const PopUpBounds box = PopUp::bounds(renderer, 1);
    PopUp::background(renderer, box);
    PopUp::title(renderer, box, "Delete " + book.title + "?");

    const int actionY = box.y + box.header;
    const int halfWidth = box.width / 2;
    const int font = systemFontId();
    const int textY = actionY + (box.row - renderer.text.getLineHeight(font)) / 2;
    const int yesWidth = renderer.text.getWidth(font, "Yes");
    const int noWidth = renderer.text.getWidth(font, "No");
    renderer.text.render(font, box.x + (halfWidth - yesWidth) / 2, textY, "Yes", true);
    renderer.text.render(font, box.x + halfWidth + (box.width - halfWidth - noWidth) / 2, textY, "No", true);
    renderer.line.render(box.x + halfWidth, actionY, box.x + halfWidth, actionY + box.row, true,
                         LineRender::Style::Dotted);
    PopUp::border(renderer, box);
    return;
  }

  const bool folder = book.type == LibraryIndex::Book::Type::FOLDER;
  const std::vector<std::string> actions = folder
                                             ? std::vector<std::string>{"Delete"}
                                             : std::vector<std::string>{
                                                   isFavorite(book) ? "Remove favorite" : "Mark as favorite",
                                                   "Mark as completed",
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
    folderDeleteConfirm = false;
    updateRequired = true;
    return true;
  }
  if (!mappedInput.hasTouch()) return false;

  float tapX = 0.0f;
  float tapY = 0.0f;
  if (!mappedInput.wasTouchTapInScreen(renderer, tapX, tapY)) return false;

  const LibraryIndex::Book book = items[static_cast<size_t>(popupBook)];
  if (folderDeleteConfirm) {
    const PopUpBounds box = PopUp::bounds(renderer, 1);
    const int x = static_cast<int>(tapX * renderer.getScreenWidth());
    const int y = static_cast<int>(tapY * renderer.getScreenHeight());
    if (x < box.x || x >= box.x + box.width || y < box.y || y >= box.y + box.height) {
      popupBook = -1;
      folderDeleteConfirm = false;
      updateRequired = true;
      return true;
    }
    if (y < box.y + box.header) return true;
    const int actionY = box.y + box.header;
    if (y >= actionY && y < actionY + box.row) {
      if (x < box.x + box.width / 2) {
        eraseFolder(book);
      } else {
        popupBook = -1;
        folderDeleteConfirm = false;
        updateRequired = true;
      }
    }
    return true;
  }

  const bool folder = book.type == LibraryIndex::Book::Type::FOLDER;
  const int actionCount = folder ? 1 : 4;
  const PopUpBounds box = PopUp::bounds(renderer, actionCount);
  const int x = static_cast<int>(tapX * renderer.getScreenWidth());
  const int y = static_cast<int>(tapY * renderer.getScreenHeight());
  if (x < box.x || x >= box.x + box.width || y < box.y || y >= box.y + box.height) {
    popupBook = -1;
    folderDeleteConfirm = false;
    updateRequired = true;
    return true;
  }

  const int action = (y - box.y - box.header) / box.row;
  if (action < 0 || action >= actionCount) return true;
  if (folder) {
    if (action == 0) {
      folderDeleteConfirm = true;
      updateRequired = true;
    }
    return true;
  }
  if (action == 0) {
    markFavorite(book);
  } else if (action == 1) {
    markCompleted(book);
  } else if (action == 2) {
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

void Library::markCompleted(const LibraryIndex::Book& book) {
  BOOK_STATE.setFinished(book.path, true, book.title);
  RECENT_BOOKS.updateProgress(book.path, 1.0f);
  popupBook = -1;
  load();
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

void Library::eraseFolder(const LibraryIndex::Book& folder) {
  if (!isDeletableFolder(folder)) {
    popupBook = -1;
    folderDeleteConfirm = false;
    updateRequired = true;
    return;
  }

  bool deleted = false;
  int removed = 0;
  {
    SdIoMutex::Lock lock;
    deleted = removeTree(folder.path, removed);
  }
  if (!deleted) {
    popupBook = -1;
    folderDeleteConfirm = false;
    updateRequired = true;
    return;
  }

  const std::string prefix = folder.path + "/";
  for (const LibraryIndex::Book& item : books) {
    if (item.type != LibraryIndex::Book::Type::BOOK ||
        item.path.compare(0, prefix.size(), prefix) != 0) {
      continue;
    }
    BOOK_STATE.removeBook(item.path);
    RECENT_BOOKS.removeBook(item.path);
    favorites.erase(item.path);
  }
  EpubNotesIndex::invalidate();
  gThumbPage.erase(folder.path);
  items.erase(std::remove_if(items.begin(), items.end(), [&folder](const LibraryIndex::Book& item) {
                return item.path == folder.path;
              }),
              items.end());
  books.erase(std::remove_if(books.begin(), books.end(), [&prefix](const LibraryIndex::Book& item) {
                return item.path.compare(0, prefix.size(), prefix) == 0;
              }),
              books.end());

  popupBook = -1;
  folderDeleteConfirm = false;
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

int Library::miniMenuX() const {
  if (SETTINGS.hiddenMenu != 0) {
    constexpr int bottomBarRightMargin = 0;
    constexpr int bottomBarButtonCount = 3;
    return std::max(0, renderer.getScreenWidth() - bottomBarRightMargin - buttonSize * bottomBarButtonCount);
  }
  return std::max(0, renderer.getScreenWidth() - buttonSize);
}

int Library::miniMenuY() const {
  if (SETTINGS.hiddenMenu != 0) return renderer.getScreenHeight() - bottomBarHeight;
  return (renderer.getScreenHeight() - (buttonSize * 2 + buttonGap)) / 2;
}

void Library::center() const {
  const int x = miniMenuX();
  const int y = miniMenuY();
  const int screenWidth = renderer.getScreenWidth();
  const bool bottomBar = SETTINGS.hiddenMenu != 0;
  const int bottomBarButtonCount = 3;
  const int containerWidth = std::min(screenWidth - x, bottomBar ? buttonSize * bottomBarButtonCount : buttonSize);
  const int containerHeight = bottomBar ? bottomBarHeight : buttonSize * 2 + buttonGap;
  renderer.rectangle.fill(x, y, containerWidth, containerHeight, false);
  // Keep the controls connected. In sidebar mode they become a centered
  // bottom bar; otherwise they remain the right-edge vertical controls.
  constexpr int rightCornerRestore = 4;
  if (!bottomBar) {
    renderer.rectangle.render(x, y, containerWidth, containerHeight, true, true);
    renderer.line.render(x + std::max(0, containerWidth - rightCornerRestore), y,
                         x + containerWidth - 1, y, true);
    renderer.line.render(x + std::max(0, containerWidth - rightCornerRestore), y + containerHeight - 1,
                         x + containerWidth - 1, y + containerHeight - 1, true);
    renderer.line.render(x, y + buttonSize, x + containerWidth, y + buttonSize, true);
  } else {
    renderer.line.render(x + buttonSize, y, x + buttonSize, y + containerHeight, true);
    renderer.line.render(x + buttonSize * 2, y, x + buttonSize * 2, y + containerHeight, true);
    renderer.line.render(x, y + containerHeight - 1, x + containerWidth, y + containerHeight - 1, true);
    renderer.line.render(0, y, screenWidth, y, true);

    int currentPage = 0;
    int totalPages = 1;
    switch (view) {
      case View::List:
        currentPage = list.currentPage();
        totalPages = list.totalPages();
        break;
      case View::Grid:
        currentPage = grid.currentPage();
        totalPages = grid.totalPages();
        break;
      case View::ListThumb:
        currentPage = listThumb.currentPage();
        totalPages = listThumb.totalPages();
        break;
      case View::Thumb:
        currentPage = thumb.currentPage();
        totalPages = thumb.totalPages();
        break;
    }
    currentPage = std::max(0, std::min(currentPage, totalPages - 1));
    const std::string pagination = std::to_string(currentPage + 1) + " / " + std::to_string(totalPages);
    const int paginationFont = systemFontId();
    renderer.text.render(paginationFont, 20,
                         y + (containerHeight - renderer.text.getLineHeight(paginationFont)) / 2,
                         pagination.c_str(), true, EpdFontFamily::REGULAR);
  }
  const int caretX = x + (buttonSize - kCaretIconSize) / 2;
  const int caretY = y + (bottomBar ? (containerHeight - kCaretIconSize) / 2
                                    : (buttonSize - kCaretIconSize) / 2);
  renderer.bitmap.iconScaled(LibraryFilterRight, caretX, caretY, kCaretSourceWidth, kCaretSourceHeight,
                             kCaretIconSize, kCaretIconSize,
                             bottomBar ? BitmapRender::Orientation::Rotate270CW : BitmapRender::Orientation::Rotate180);
  if (bottomBar) {
    renderer.bitmap.icon(Search, x + buttonSize + (buttonSize - kRefreshIconSize) / 2,
                         y + (containerHeight - kRefreshIconSize) / 2, kRefreshIconSize, kRefreshIconSize);
  }
  const int refreshX = x + (bottomBar ? buttonSize * 2 : 0) + (buttonSize - kRefreshIconSize) / 2;
  const int refreshY = y + (bottomBar ? (containerHeight - kRefreshIconSize) / 2
                                      : buttonSize + buttonGap + (buttonSize - kRefreshIconSize) / 2);
  renderer.bitmap.icon(Refresh, refreshX, refreshY, kRefreshIconSize, kRefreshIconSize);
}

bool Library::handleSidebarTap() {
  if (!mappedInput.hasTouch()) return false;

  float tapNx = 0.0f;
  float tapNy = 0.0f;
  if (!mappedInput.wasTouchTapInScreen(renderer, tapNx, tapNy)) return false;

  const int tapX = static_cast<int>(tapNx * renderer.getScreenWidth());
  const int tapY = static_cast<int>(tapNy * renderer.getScreenHeight());
  if (navigation::Menu::hamburgerHit(tapX, tapY)) {
    storeOverlayFrame();
    sidebarOpen = true;
    sidebarScrollOffset_ = 0;
    updateRequired = true;
    renderPage();
    return true;
  }

  mappedInput.restoreTouchTapInScreen(renderer, tapNx, tapNy);
  return false;
}

bool Library::handleSidebarInput() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back) || mappedInput.wasTouchSwipeRight()) {
    restoreOverlayFrameOnClose();
    sidebarOpen = false;
    updateRequired = true;
    return true;
  }
  PluginManager::LibraryMenuLink pluginMenu;
  const bool hasPluginMenu = PluginManager::findLibraryMenuPlugin(pluginMenu);
  const size_t sidebarItemCount = hasPluginMenu ? 9 : 8;
  if (mappedInput.hasTouch() && (mappedInput.wasTouchSwipeUp() || mappedInput.wasTouchSwipeDown())) {
    const int maxOffset = std::max(0, static_cast<int>(sidebarItemCount) - Sidebar::visibleRows(renderer));
    const int step = std::max(1, Sidebar::visibleRows(renderer) - 1);
    const int nextOffset = std::max(0, std::min(maxOffset, sidebarScrollOffset_ +
        (mappedInput.wasTouchSwipeUp() ? step : -step)));
    if (nextOffset != sidebarScrollOffset_) {
      sidebarScrollOffset_ = nextOffset;
      updateRequired = true;
    }
    return true;
  }
  if (!mappedInput.hasTouch()) return false;

  float tapNx = 0.0f;
  float tapNy = 0.0f;
  if (!mappedInput.wasTouchTapInScreen(renderer, tapNx, tapNy)) return false;

  const int tapX = static_cast<int>(tapNx * renderer.getScreenWidth());
  const int tapY = static_cast<int>(tapNy * renderer.getScreenHeight());
  const int drawerWidth = Sidebar::width(renderer);
  if (tapX >= drawerWidth) {
    restoreOverlayFrameOnClose();
    sidebarOpen = false;
    updateRequired = true;
    return true;
  }

  const int item = Sidebar::hitTest(renderer, tapX, tapY, sidebarItemCount, sidebarScrollOffset_);
  if (item == 0 || item == 1) {
    discardOverlayFrame();
    stateFilter = StateFilter::None;
    pluginMode_ = false;
    metadataGroupKey_.clear();
    const bool showAllBooks = item == 0;
    SETTINGS.libraryViewMode = showAllBooks ? SystemSetting::LIBRARY_VIEW_BOOKS
                                           : SystemSetting::LIBRARY_VIEW_FOLDERS;
    SETTINGS.saveToFile();
    sidebarOpen = false;
    if (path != "/") {
      gThumbPage[path] = thumb.currentPage();
      onGoToLibrary("/");
      return true;
    } else {
      allBooksMode = showAllBooks;
      sortOpen = false;
      filterOpen = false;
      popupBook = -1;
      thumb.setPage(0);
      load();
    }
    updateRequired = true;
  } else if (item >= 2 && item <= 4) {
    discardOverlayFrame();
    stateFilter = item == 2 ? StateFilter::Favorites
                            : (item == 3 ? StateFilter::Reading : StateFilter::Finished);
    SETTINGS.libraryViewMode = item == 2 ? SystemSetting::LIBRARY_VIEW_FAVORITES
                                         : (item == 3 ? SystemSetting::LIBRARY_VIEW_READING
                                                      : SystemSetting::LIBRARY_VIEW_FINISHED);
    SETTINGS.saveToFile();
    pluginMode_ = false;
    metadataGroupKey_.clear();
    allBooksMode = false;
    sortOpen = false;
    filterOpen = false;
    popupBook = -1;
    thumb.setPage(0);
    sidebarOpen = false;
    load();
    updateRequired = true;
  } else if (item >= 5 && item <= 7) {
    discardOverlayFrame();
    metadataKind_ = item == 5 ? MetadataIndex::Kind::Authors
                              : (item == 6 ? MetadataIndex::Kind::Tags : MetadataIndex::Kind::Series);
    SETTINGS.libraryViewMode = item == 5 ? SystemSetting::LIBRARY_VIEW_AUTHORS
                                         : (item == 6 ? SystemSetting::LIBRARY_VIEW_TAGS
                                                      : SystemSetting::LIBRARY_VIEW_SERIES);
    SETTINGS.saveToFile();
    metadataGroupKey_.clear();
    metadataIndexAvailable_ = false;
    stateFilter = StateFilter::Metadata;
    resetFilterSelections();
    filterCategory_ = FilterCategory::Categories;
    filterMetadataGroups_.clear();
    filterMetadataIndexAvailable_ = false;
    filterScrollOffset_ = 0;
    pluginMode_ = false;
    allBooksMode = false;
    sortOpen = false;
    filterOpen = false;
    popupBook = -1;
    thumb.setPage(0);
    sidebarOpen = false;
    sidebarScrollOffset_ = 0;
    load();
    updateRequired = true;
  } else if (hasPluginMenu && item == 8) {
    discardOverlayFrame();
    pluginMenu_ = pluginMenu;
    pluginMode_ = true;
    stateFilter = StateFilter::Plugin;
    SETTINGS.libraryViewMode = SystemSetting::LIBRARY_VIEW_PLUGIN;
    SETTINGS.saveToFile();
    path = "/";
    activePluginGroup_.clear();
    allBooksMode = false;
    sortOpen = false;
    filterOpen = false;
    popupBook = -1;
    thumb.setPage(0);
    sidebarOpen = false;
    sidebarScrollOffset_ = 0;
    load();
    updateRequired = true;
  }
  return true;
}

void Library::drawSidebar() const {
  PluginManager::LibraryMenuLink pluginMenu;
  const bool hasPluginMenu = PluginManager::findLibraryMenuPlugin(pluginMenu);
  const char* labels[9] = {"All books", "Folders", "Favorites", "Reading", "Finished",
                           "Authors", "Tags", "Series", nullptr};
  if (hasPluginMenu) labels[8] = pluginMenu.label.c_str();
  Sidebar::renderFrame(renderer, "Library");
  Sidebar::renderTextList(renderer, labels, hasPluginMenu ? 9 : 8, sidebarScrollOffset_);
}

navigation::Menu::Action Library::centerTap(const int tapX, const int tapY) const {
  const int x = miniMenuX();
  const int y = miniMenuY();
  const int touchPadding = refreshTouchPadding;
  if (SETTINGS.hiddenMenu != 0) {
    if (tapY >= y - touchPadding && tapY < y + bottomBarHeight + touchPadding) {
      if (tapX >= x - touchPadding && tapX < x + buttonSize + touchPadding) {
        return navigation::Menu::Action::Filter;
      }
      if (tapX >= x + buttonSize - touchPadding && tapX < x + buttonSize * 2 + touchPadding) {
        return navigation::Menu::Action::Search;
      }
      if (tapX >= x + buttonSize * 2 - touchPadding && tapX < x + buttonSize * 3 + touchPadding) {
        return navigation::Menu::Action::Refresh;
      }
    }
    return navigation::Menu::Action::None;
  }
  if (tapX >= x - touchPadding && tapX < x + buttonSize + touchPadding && tapY >= y &&
      tapY < y + buttonSize) {
    return navigation::Menu::Action::Filter;
  }
  const int refreshY = y + buttonSize + buttonGap;
  if (tapX >= x - touchPadding && tapX < x + buttonSize + touchPadding && tapY >= refreshY &&
      tapY < refreshY + buttonSize) {
    return navigation::Menu::Action::Refresh;
  }
  return navigation::Menu::Action::None;
}

bool Library::menuAction(const navigation::Menu::Action action) {
  if (action == navigation::Menu::Action::View || action == navigation::Menu::Action::Sort ||
      action == navigation::Menu::Action::Filter) {
    controlModalOpen = true;
    controlTab = action == navigation::Menu::Action::View
                     ? ControlTab::View
                     : (action == navigation::Menu::Action::Sort ? ControlTab::Sort : ControlTab::Filter);
    sortOpen = false;
    filterOpen = false;
    if (controlTab == ControlTab::Filter) openFilterCategory(FilterCategory::Categories);
    updateRequired = true;
    return true;
  }
  return false;
}

void Library::menu() {
  UiPage::menu();
  if (controlModalOpen) controlModal();
  if (popupBook >= 0 && popupBook < static_cast<int>(items.size())) popup();
  if (sidebarOpen) drawSidebar();
}

void Library::controlModal() const {
  const ControlModalLayout modal = controlModalLayout(renderer);
  ScreenComponents::drawWhiteOverlay(renderer, modal.y);
  renderer.rectangle.fill(modal.x, modal.y, modal.width, modal.height, false);
  const int tabDividerY = modal.tabY + modal.tabHeight;
  renderer.rectangle.fill(modal.x, tabDividerY, modal.width, 1,
                          static_cast<int>(GfxRenderer::FillTone::Gray), false);
  const uint8_t* sortIcon = sort == Sort::TitleZA || sort == Sort::FolderZA || sort == Sort::AuthorZA ? SortDesc : SortAsc;
  renderControlTab(renderer, modal, 0, "View", viewIcon(), controlTab == ControlTab::View);
  renderControlTab(renderer, modal, 1, "Sort", sortIcon, controlTab == ControlTab::Sort);
  renderControlTab(renderer, modal, 2, "Filter", Filter, controlTab == ControlTab::Filter);

  switch (controlTab) {
    case ControlTab::View:
      viewDropdown();
      break;
    case ControlTab::Sort:
      sortDropdown();
      break;
    case ControlTab::Filter:
      filterPopup();
      break;
  }
  renderer.rectangle.render(modal.x, modal.y, modal.width, modal.height, true);
}

bool Library::controlModalInput() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (controlTab == ControlTab::Filter && filterCategory_ != FilterCategory::Categories) {
      openFilterCategory(FilterCategory::Categories);
    } else {
      controlModalOpen = false;
    }
    updateRequired = true;
    return true;
  }

  const ControlModalLayout modal = controlModalLayout(renderer);
  if (mappedInput.hasTouch()) {
    float swipeNx = 0.0f;
    float swipeNy = 0.0f;
    const bool swipeUp = mappedInput.wasTouchSwipeUpInScreen(renderer, swipeNx, swipeNy);
    const bool swipeDown = !swipeUp && mappedInput.wasTouchSwipeDownInScreen(renderer, swipeNx, swipeNy);
    if (swipeUp || swipeDown) {
      const int startX = static_cast<int>(swipeNx * renderer.getScreenWidth());
      const int startY = static_cast<int>(swipeNy * renderer.getScreenHeight());
      const bool insideModal = startX >= modal.x && startX < modal.x + modal.width && startY >= modal.y &&
                               startY < modal.y + modal.height;
      if (controlTab == ControlTab::Filter && insideModal) {
        const FilterDrawerLayout drawer = filterDrawerLayout(renderer, filterRowCount());
        const int maxScroll = std::max(0, filterRowCount() - drawer.visibleRows);
        const int scrollStep = std::max(1, drawer.visibleRows - 1);
        if (startX >= drawer.x && startX < drawer.x + drawer.width && startY >= drawer.y &&
            startY < drawer.y + drawer.height) {
          if (swipeUp) {
            filterScrollOffset_ = std::min(maxScroll, filterScrollOffset_ + scrollStep);
          } else if (filterScrollOffset_ > 0) {
            filterScrollOffset_ = std::max(0, filterScrollOffset_ - scrollStep);
          } else if (filterCategory_ != FilterCategory::Categories) {
            openFilterCategory(FilterCategory::Categories);
          }
        } else if (swipeDown) {
          controlModalOpen = false;
        }
      } else if (swipeDown) {
        controlModalOpen = false;
      }
      updateRequired = true;
      return true;
    }

    float tapNx = 0.0f;
    float tapNy = 0.0f;
    if (mappedInput.wasTouchTapInScreen(renderer, tapNx, tapNy)) {
      const int tapX = static_cast<int>(tapNx * renderer.getScreenWidth());
      const int tapY = static_cast<int>(tapNy * renderer.getScreenHeight());
      if (tapX < modal.x || tapX >= modal.x + modal.width || tapY < modal.y || tapY >= modal.y + modal.height) {
        controlModalOpen = false;
        updateRequired = true;
        return true;
      }
      if (tapY >= modal.tabY && tapY < modal.tabY + modal.tabHeight) {
        const int tab = std::min(2, std::max(0, (tapX - modal.x) * 3 / modal.width));
        controlTab = tab == 0 ? ControlTab::View : (tab == 1 ? ControlTab::Sort : ControlTab::Filter);
        if (controlTab == ControlTab::Filter) openFilterCategory(FilterCategory::Categories);
        updateRequired = true;
        return true;
      }

      if (controlTab == ControlTab::View) {
        constexpr int rowHeight = kControlListItemHeight;
        const int listY = modal.contentY;
        const int listX = modal.x;
        const int listWidth = modal.width;
        if (tapX >= listX && tapX < listX + listWidth && tapY >= listY && tapY < listY + 4 * rowHeight) {
          applyView((tapY - listY) / rowHeight);
        }
        return true;
      }
      if (controlTab == ControlTab::Sort) {
        handleSortTap(tapX, tapY);
        return true;
      }
      handleFilterTap(tapX, tapY);
      return true;
    }
  }
  return false;
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
    case View::ListThumb:
      return LibraryViewList;
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
  listThumb.reset();
  thumb.reset();
  if (view == View::ListThumb) {
    listThumbDeferImageDecode_ = true;
    listThumbWarmupPending_ = false;
  }
}

void Library::sortDropdown() const {
  const ControlModalLayout modal = controlModalLayout(renderer);
  constexpr int leftPadding = 24;
  constexpr int rightPadding = 30;
  constexpr int radioRadius = 10;
  const int font = systemFontId();
  const int x = modal.x;
  const int y = modal.contentY;
  const int width = modal.width;
  constexpr int rowHeight = kControlListItemHeight;
  const char* names[] = {"Title", "Title", "Folder", "Folder", "Author", "Author"};
  const char* directions[] = {"A-Z", "Z-A", "A-Z", "Z-A", "A-Z", "Z-A"};
  for (int index = 0; index < sortCount(); ++index) {
    const int rowY = y + index * rowHeight;
    const bool selected = index == sortIndex();
    const int textY = rowY + (rowHeight - renderer.text.getLineHeight(font)) / 2;
    renderer.text.render(font, x + leftPadding, textY, names[index], true, EpdFontFamily::REGULAR);
    const int directionFont = systemFontIdForPointSize(8);
    const int directionWidth = renderer.text.getWidth(directionFont, directions[index]);
    const int directionY = rowY + (rowHeight - renderer.text.getLineHeight(directionFont)) / 2;
    renderer.text.render(directionFont, x + width - rightPadding - directionWidth - radioRadius * 2 - 16,
                         directionY, directions[index], true, EpdFontFamily::REGULAR);
    const int radioX = x + width - rightPadding - radioRadius;
    const int radioY = rowY + rowHeight / 2;
    renderer.circle.render(radioX, radioY, radioRadius, true);
    renderer.circle.render(radioX, radioY, radioRadius - 3, false);
    if (selected) renderer.circle.render(radioX, radioY, radioRadius - 6, true);
    if (index + 1 < sortCount()) {
      renderer.line.render(x + 10, rowY + rowHeight, x + width - 10, rowY + rowHeight, true,
                           LineRender::Style::Dotted);
    }
  }
}

void Library::handleSortTap(const int tapX, const int tapY) {
  const ControlModalLayout modal = controlModalLayout(renderer);
  const int x = modal.x;
  const int y = modal.contentY;
  const int width = modal.width;
  constexpr int rowHeight = kControlListItemHeight;
  if (tapX >= x && tapX < x + width && tapY >= y && tapY < y + sortCount() * rowHeight) {
    applySort((tapY - y) / rowHeight);
    return;
  }
  controlModalOpen = false;
  updateRequired = true;
}

void Library::viewDropdown() const {
  const ControlModalLayout modal = controlModalLayout(renderer);
  constexpr int rowHeight = kControlListItemHeight;
  const int font = systemFontId();
  const int x = modal.x;
  const int y = modal.contentY;
  const int width = modal.width;
  const char* labels[] = {"List", "Grid", "Thumbnails", "Thumbnail list"};
  for (int index = 0; index < 4; ++index) {
    const int rowY = y + index * rowHeight;
    const bool selected = static_cast<int>(view) == index;
    const int iconSize = 40;
    const uint8_t* icon = index == 0 ? LibraryViewList
                                     : (index == 1 ? LibraryViewGrid
                                                   : (index == 2 ? LibraryViewThumb : LibraryViewList));
    renderer.bitmap.icon(icon,
                         x + 18, rowY + (rowHeight - iconSize) / 2, iconSize, iconSize);
    const int textY = rowY + (rowHeight - renderer.text.getLineHeight(font)) / 2;
    renderer.text.render(font, x + 18 + iconSize + 18, textY, labels[index], true, EpdFontFamily::REGULAR);
    const int radioX = x + width - 32;
    const int radioY = rowY + rowHeight / 2;
    renderer.circle.render(radioX, radioY, 10, true);
    renderer.circle.render(radioX, radioY, 7, false);
    if (selected) renderer.circle.render(radioX, radioY, 4, true);
    if (index < 3) {
      renderer.line.render(x + 12, rowY + rowHeight, x + width - 12, rowY + rowHeight, true,
                           LineRender::Style::Dotted);
    }
  }
}

void Library::applyView(const int index) {
  if (index < 0 || index >= 4) return;
  view = static_cast<View>(index);
  switch (view) {
    case View::List:
      SETTINGS.libraryMode = SystemSetting::LIBRARY_LIST;
      break;
    case View::Grid:
      SETTINGS.libraryMode = SystemSetting::LIBRARY_GRID;
      break;
    case View::Thumb:
      SETTINGS.libraryMode = SystemSetting::LIBRARY_THUMBNAIL;
      break;
    case View::ListThumb:
      SETTINGS.libraryMode = SystemSetting::LIBRARY_LIST_THUMBNAIL;
      break;
  }
  SETTINGS.saveToFile();
  load();
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

void Library::applyFilter(const int index) {
  if (index < 0 || index >= 26) return;
  titleFilters_.flip(index);
  load();
  updateRequired = true;
}

const char* Library::typeFilterLabel(const int index) {
  switch (index) {
    case 0:
      return "EPUB";
    case 1:
      return "PDF";
    case 2:
      return "TXT";
    case 3:
      return "XTC";
    default:
      return "";
  }
}

const char* Library::typeFilterCategory(const int index) {
  switch (index) {
    case 0:
      return "epub";
    case 1:
      return "pdf";
    case 2:
      return "txt";
    case 3:
      return "xtc";
    default:
      return "";
  }
}

const char* Library::filterCategoryLabel(const FilterCategory category) {
  switch (category) {
    case FilterCategory::Categories: return "Filter";
    case FilterCategory::Title: return "Title";
    case FilterCategory::Type: return "Type";
    case FilterCategory::Author: return "Author";
    case FilterCategory::Series: return "Series";
    case FilterCategory::Tags: return "Tags";
    case FilterCategory::Options: return "Options";
  }
  return "Filter";
}

void Library::applyTypeFilter(const int index) {
  if (index < 0 || index >= 4) return;
  typeFilters_.flip(index);
  load();
  updateRequired = true;
}

int Library::metadataFilterIndex(const FilterCategory category) {
  switch (category) {
    case FilterCategory::Author: return 0;
    case FilterCategory::Series: return 1;
    case FilterCategory::Tags: return 2;
    default: return -1;
  }
}

int Library::filterRowCount() const {
  switch (filterCategory_) {
    case FilterCategory::Categories: return kFilterMenuRowCount;
    case FilterCategory::Title: return 26;
    case FilterCategory::Type: return 4;
    case FilterCategory::Options: return 1;
    case FilterCategory::Author:
    case FilterCategory::Series:
    case FilterCategory::Tags:
      return std::max(1, static_cast<int>(filterMetadataGroups_.size()));
  }
  return 0;
}

int Library::selectedFilterCount(const FilterCategory category) const {
  switch (category) {
    case FilterCategory::Title: return static_cast<int>(titleFilters_.count());
    case FilterCategory::Type: return static_cast<int>(typeFilters_.count());
    case FilterCategory::Options: return SETTINGS.hideFinishedBooks != 0 ? 1 : 0;
    case FilterCategory::Author:
    case FilterCategory::Series:
    case FilterCategory::Tags: {
      const int index = metadataFilterIndex(category);
      return index >= 0 ? static_cast<int>(metadataFilters_[index].selectedKeys.size()) : 0;
    }
    case FilterCategory::Categories:
      return 0;
  }
  return 0;
}

void Library::resetFilterSelections() {
  titleFilters_.reset();
  typeFilters_.reset();
  for (MetadataFilterSelection& selection : metadataFilters_) {
    selection.selectedKeys.clear();
    selection.pathMatchCounts.clear();
  }
}

void Library::openFilterCategory(const FilterCategory category) {
  filterCategory_ = category;
  filterScrollOffset_ = 0;
  filterMetadataGroups_.clear();
  filterMetadataIndexAvailable_ = false;

  const int index = metadataFilterIndex(category);
  if (index >= 0) {
    filterMetadataIndexAvailable_ = MetadataIndex::loadGroups(metadataFilters_[index].kind, filterMetadataGroups_);
  }
  updateRequired = true;
}

void Library::applyMetadataFilter(const int index) {
  if (index < 0 || index >= static_cast<int>(filterMetadataGroups_.size())) return;
  const int metadataIndex = metadataFilterIndex(filterCategory_);
  if (metadataIndex < 0) return;

  const MetadataIndex::Group& group = filterMetadataGroups_[static_cast<size_t>(index)];
  MetadataFilterSelection& selection = metadataFilters_[metadataIndex];
  const auto selectedKey = selection.selectedKeys.find(std::string_view(group.key));
  const bool wasSelected = selectedKey != selection.selectedKeys.end();
  std::vector<MetadataIndex::Entry> entries;
  if (!MetadataIndex::loadGroup(selection.kind, group, entries)) return;

  if (wasSelected) {
    selection.selectedKeys.erase(selectedKey);
    for (const MetadataIndex::Entry& entry : entries) {
      const std::string path = cleanPath(entry.path);
      const auto match = selection.pathMatchCounts.find(std::string_view(path));
      if (match == selection.pathMatchCounts.end()) continue;
      if (match->second <= 1) {
        selection.pathMatchCounts.erase(match);
      } else {
        --match->second;
      }
    }
  } else {
    selection.selectedKeys.insert(EpubPsramString(group.key.begin(), group.key.end()));
    for (const MetadataIndex::Entry& entry : entries) {
      const std::string path = cleanPath(entry.path);
      auto match = selection.pathMatchCounts.try_emplace(
          EpubPsramString(path.begin(), path.end()), 0).first;
      ++match->second;
    }
  }

  load();
  updateRequired = true;
}

void Library::clearFilters() {
  bool hasSelection = titleFilters_.any() || typeFilters_.any();
  for (const MetadataFilterSelection& selection : metadataFilters_) {
    hasSelection = hasSelection || !selection.selectedKeys.empty();
  }
  if (!hasSelection) return;
  resetFilterSelections();
  load();
  updateRequired = true;
}

void Library::filterPopup() const {
  const int rowCount = filterRowCount();
  const FilterDrawerLayout drawer = filterDrawerLayout(renderer, rowCount);
  const int maxScroll = std::max(0, rowCount - drawer.visibleRows);
  const int scroll = std::clamp(filterScrollOffset_, 0, maxScroll);
  constexpr int horizontalPadding = 24;
  const int font = systemFontId();

  const int lineHeight = renderer.text.getLineHeight(font);
  const int listY = drawer.y;
  static constexpr const char* kCategories[kFilterCategoryCount] = {
      "Title", "Type", "Author", "Series", "Tags"};
  const int metadataIndex = metadataFilterIndex(filterCategory_);
  const bool metadataHasValues = metadataIndex >= 0 && filterMetadataIndexAvailable_ &&
                                 !filterMetadataGroups_.empty();

  const int rowsToDraw = std::max(0, std::min(drawer.visibleRows, rowCount - scroll));
  for (int visibleRow = 0; visibleRow < rowsToDraw; ++visibleRow) {
    const int rowIndex = scroll + visibleRow;
    const int contentIndex = rowIndex;
    const int rowY = listY + visibleRow * drawer.rowHeight;
    const int textY = rowY + (drawer.rowHeight - lineHeight) / 2;
    bool checked = false;
    const char* label = nullptr;
    std::string value;
    char letterLabel[2] = {};

    if (filterCategory_ == FilterCategory::Categories) {
      if (contentIndex < kFilterCategoryCount) {
        label = kCategories[contentIndex];
        const FilterCategory category =
            static_cast<FilterCategory>(static_cast<int>(FilterCategory::Title) + contentIndex);
        const int count = selectedFilterCount(category);
        if (count > 0) value = std::to_string(count);
      } else {
        label = "Hide finished books";
        checked = SETTINGS.hideFinishedBooks != 0;
      }
    } else if (filterCategory_ == FilterCategory::Title) {
      letterLabel[0] = static_cast<char>('A' + contentIndex);
      label = letterLabel;
      checked = titleFilters_.test(contentIndex);
    } else if (filterCategory_ == FilterCategory::Type) {
      label = typeFilterLabel(contentIndex);
      checked = typeFilters_.test(contentIndex);
    } else if (metadataIndex >= 0) {
      if (!filterMetadataIndexAvailable_) {
        label = "No metadata index";
      } else if (filterMetadataGroups_.empty()) {
        label = "No metadata found";
      } else {
        const MetadataIndex::Group& group = filterMetadataGroups_[static_cast<size_t>(contentIndex)];
        label = group.label.c_str();
        checked = metadataFilters_[metadataIndex].selectedKeys.find(std::string_view(group.key)) !=
                  metadataFilters_[metadataIndex].selectedKeys.end();
      }
    } else if (filterCategory_ == FilterCategory::Options) {
      label = "Hide finished books";
      checked = SETTINGS.hideFinishedBooks != 0;
    }

    const bool isCategoryEntry = filterCategory_ == FilterCategory::Categories &&
                                 contentIndex >= 0 && contentIndex < kFilterCategoryCount;
    if (isCategoryEntry) {
      renderer.text.render(font, drawer.x + horizontalPadding, textY, label, true, EpdFontFamily::REGULAR);
      constexpr int caretSize = 30;
      const int arrowX = drawer.x + drawer.width - horizontalPadding - caretSize;
      const int arrowY = rowY + (drawer.rowHeight - caretSize) / 2;
      if (!value.empty()) {
        const int valueWidth = renderer.text.getWidth(font, value.c_str(), EpdFontFamily::REGULAR);
        renderer.text.render(font, arrowX - 16 - valueWidth, textY, value.c_str(), true,
                             EpdFontFamily::REGULAR);
      }
      renderer.bitmap.iconScaled(LibraryFilterRight, arrowX, arrowY, kCaretSourceWidth, kCaretSourceHeight,
                                 caretSize, caretSize);
    } else if (label) {
      const ToggleBounds toggle = Toggle::bounds(drawer.x + drawer.width - horizontalPadding, rowY,
                                                 drawer.rowHeight);
      const int maxTextWidth = std::max(1, toggle.x - (drawer.x + horizontalPadding) - 12);
      const std::string shown = renderer.text.truncate(font, label, maxTextWidth, EpdFontFamily::REGULAR);
      renderer.text.render(font, drawer.x + horizontalPadding, textY, shown.c_str(), true,
                           EpdFontFamily::REGULAR);
      if ((filterCategory_ == FilterCategory::Categories && contentIndex == kFilterCategoryCount) ||
          (filterCategory_ != FilterCategory::Categories && filterCategory_ != FilterCategory::Author &&
          filterCategory_ != FilterCategory::Series &&
          filterCategory_ != FilterCategory::Tags)) {
        Toggle::render(renderer, toggle, checked);
      } else if ((filterCategory_ == FilterCategory::Author || filterCategory_ == FilterCategory::Series ||
                  filterCategory_ == FilterCategory::Tags) && metadataHasValues) {
        Toggle::render(renderer, toggle, checked);
      }
    }

    if (visibleRow + 1 < rowsToDraw) {
      renderer.line.render(drawer.x + horizontalPadding, rowY + drawer.rowHeight,
                           drawer.x + drawer.width - horizontalPadding, rowY + drawer.rowHeight, true,
                           LineRender::Style::Dotted);
    }
  }

  renderFilterScrollbar(renderer, drawer, rowCount, drawer.visibleRows, scroll);
  const int doneY = drawer.y + drawer.height - drawer.doneHeight;
  renderer.line.render(drawer.x, doneY, drawer.x + drawer.width, doneY, true);
  const FilterFooterLayout footer = filterFooterLayout(renderer, drawer,
                                                       filterCategory_ != FilterCategory::Categories, font);
  if (footer.hasBack) {
    const int caretX = footer.back.x + (footer.back.width - footer.caretSize) / 2;
    const int caretY = footer.back.y + (footer.back.height - footer.caretSize) / 2;
    renderer.bitmap.iconScaled(LibraryFilterLeft, caretX, caretY, kCaretSourceWidth, kCaretSourceHeight,
                               footer.caretSize, footer.caretSize);
  }
  Button::render(renderer, footer.clear, "Clear", false, font);
  Button::render(renderer, footer.done, "Done", true, font);
}

void Library::handleFilterTap(const int tapX, const int tapY) {
  const int rowCount = filterRowCount();
  const FilterDrawerLayout drawer = filterDrawerLayout(renderer, rowCount);

  if (tapY < drawer.y || tapY >= drawer.y + drawer.height || tapX < drawer.x || tapX >= drawer.x + drawer.width) {
    controlModalOpen = false;
    updateRequired = true;
    return;
  }

  const int listY = drawer.y + drawer.headerHeight;
  const int doneY = drawer.y + drawer.height - drawer.doneHeight;
  if (tapY >= doneY) {
    const FilterFooterLayout footer = filterFooterLayout(
        renderer, drawer, filterCategory_ != FilterCategory::Categories, systemFontId());
    if (footer.hasBack && tapX >= footer.back.x && tapX < footer.back.x + footer.back.width) {
      openFilterCategory(FilterCategory::Categories);
    } else if (tapX >= footer.clear.x && tapX < footer.clear.x + footer.clear.width) {
      clearFilters();
    } else if (tapX >= footer.done.x && tapX < footer.done.x + footer.done.width) {
      controlModalOpen = false;
      updateRequired = true;
    }
    return;
  }

  const int maxScroll = std::max(0, rowCount - drawer.visibleRows);
  if (tapY < listY) return;
  const int visibleRow = (tapY - listY) / drawer.rowHeight;
  const int rowIndex = std::clamp(filterScrollOffset_, 0, maxScroll) + visibleRow;
  if (visibleRow < 0 || visibleRow >= drawer.visibleRows || rowIndex >= rowCount) return;

  if (filterCategory_ == FilterCategory::Categories) {
    const FilterCategory categories[kFilterCategoryCount] = {
        FilterCategory::Title, FilterCategory::Type, FilterCategory::Author,
        FilterCategory::Series, FilterCategory::Tags};
    if (rowIndex < kFilterCategoryCount) {
      openFilterCategory(categories[rowIndex]);
    } else if (rowIndex == kFilterCategoryCount) {
      SETTINGS.hideFinishedBooks = SETTINGS.hideFinishedBooks == 0 ? 1 : 0;
      SETTINGS.saveToFile();
      load();
      updateRequired = true;
    }
    return;
  }

  const int contentIndex = rowIndex;
  if (filterCategory_ == FilterCategory::Title) {
    applyFilter(contentIndex);
  } else if (filterCategory_ == FilterCategory::Type) {
    applyTypeFilter(contentIndex);
  } else if (metadataFilterIndex(filterCategory_) >= 0 && filterMetadataIndexAvailable_) {
    applyMetadataFilter(contentIndex);
  } else if (filterCategory_ == FilterCategory::Options && contentIndex == 0) {
    SETTINGS.hideFinishedBooks = SETTINGS.hideFinishedBooks == 0 ? 1 : 0;
    SETTINGS.saveToFile();
    load();
    updateRequired = true;
  }
}
