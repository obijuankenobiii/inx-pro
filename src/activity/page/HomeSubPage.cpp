#include "HomeSubPage.h"
#include "system/UiLayout.h"

#include <Arduino.h>
#include <Epub/BookMetadataCache.h>
#include <GfxRenderer.h>
#include <SDCardManager.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "activity/reader/Epub/EpubAnnotationStorage.h"
#include "activity/reader/Epub/EpubBookmarks.h"
#include "activity/reader/Epub/GeminiTranscription.h"
#include "components/home/Preview.h"
#include "components/global/Button.h"
#include "images/Close.h"
#include "images/Download.h"
#include "images/Trash.h"
#include "activity/util/KeyboardEntryActivity.h"
#include "dictionary/StarDictLookup.h"
#include "state/BookState.h"
#include "state/RecentBooks.h"
#include "state/ReaderSetting.h"
#include "state/SavedDictionaryWords.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"

extern void openReaderFromCallback(const std::string& path, std::function<void()> returnToCaller);
extern void openReaderFromCallback(const std::string& path, std::function<void()> returnToCaller, int spineIndex,
                                   int pageNumber);
extern void onGoToHome();
extern void openDictionaryLookupKeyboard();

namespace {

const char* label(const HomeSubPage::Section section) {
  switch (section) {
    case HomeSubPage::Section::Bookmarks:
      return "Bookmarks";
    case HomeSubPage::Section::Highlights:
      return "Highlights";
    case HomeSubPage::Section::Favorites:
      return "Favorites";
    case HomeSubPage::Section::Dictionary:
      return "Dictionary";
  }
  return "";
}

const char* emptyState(const HomeSubPage::Section section) {
  switch (section) {
    case HomeSubPage::Section::Bookmarks:
      return "No bookmarks yet";
    case HomeSubPage::Section::Highlights:
      return "No highlights yet";
    case HomeSubPage::Section::Favorites:
      return "No favorites yet";
    case HomeSubPage::Section::Dictionary:
      return "No saved words yet";
  }
  return "";
}

constexpr int contentTop = 80;
constexpr int contentBottom = 20;
constexpr int rowHeight = UiLayout::LIST_ITEM_HEIGHT;
constexpr int bookGroupHeaderHeight = 42;
constexpr int deleteSize = 40;
constexpr int deleteHitPadding = 15;
constexpr int dictionaryLookupButtonGap = 12;
constexpr int openPageIconSize = 40;
constexpr int openPageActionPadding = 5;
constexpr int openPageActionSize = openPageIconSize + openPageActionPadding * 2;

ButtonBounds openPageActionBounds(const GfxRenderer& renderer) {
  return {renderer.getScreenWidth() - 20 - openPageActionSize,
          renderer.getScreenHeight() - 20 - openPageActionSize, openPageActionSize, openPageActionSize};
}

ButtonBounds dictionaryLookupButtonBounds(const GfxRenderer& renderer) {
  const int font = systemFontId();
  const int width = Button::width(renderer, "Look up", font);
  const int y = renderer.getScreenHeight() - contentBottom - Button::height;
  return {renderer.getScreenWidth() - 20 - width, y, width, Button::height};
}

std::string cachedTitle(const std::string& cachePath) {
  BookMetadataCache metadata(cachePath);
  if (metadata.load() && !metadata.coreMetadata.title.empty()) {
    return metadata.coreMetadata.title;
  }

  for (const RecentBook& book : RECENT_BOOKS.getBooks()) {
    const std::string bookCache =
        book.cachePath.empty() ? "/.metadata/epub/" + std::to_string(std::hash<std::string>{}(book.path))
                               : book.cachePath;
    if (bookCache == cachePath && !book.title.empty()) {
      return book.title;
    }
  }
  return "Untitled book";
}

std::string cachePathForBookPath(const std::string& bookPath) {
  return "/.metadata/epub/" + std::to_string(std::hash<std::string>{}(bookPath));
}

std::string bookPathForCachePath(const std::string& cachePath) {
  for (const RecentBook& book : RECENT_BOOKS.getBooks()) {
    if (book.path.empty()) continue;
    const std::string bookCache = book.cachePath.empty() ? cachePathForBookPath(book.path) : book.cachePath;
    if (bookCache == cachePath) return book.path;
  }

  for (const BookState::Book& book : BOOK_STATE.getAllBooks()) {
    if (!book.path.empty() && cachePathForBookPath(book.path) == cachePath) return book.path;
  }
  return {};
}

std::string bookmarkLabel(const std::string& bookTitle, const EpubBookmark& bookmark) {
  char chapterTitle[sizeof(bookmark.chapterTitle) + 1] = {};
  std::memcpy(chapterTitle, bookmark.chapterTitle, sizeof(bookmark.chapterTitle));
  char pageLabel[24] = {};
  std::snprintf(pageLabel, sizeof(pageLabel), " p%d", static_cast<int>(bookmark.pageNumber) + 1);
  if (chapterTitle[0] == '\0') {
    return bookTitle + pageLabel;
  }
  return bookTitle + " - " + std::string(chapterTitle) + pageLabel;
}

std::string trimText(std::string text) {
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
    text.erase(text.begin());
  }
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
    text.pop_back();
  }
  return text;
}

}

HomeSubPage::HomeSubPage(GfxRenderer& renderer, MappedInputManager& mappedInput, const Section section,
                         std::function<void()> close, std::string lookupWord)
    : SubPage(label(section), renderer, mappedInput, std::move(close)), section(section), lookupWord_(std::move(lookupWord)) {}

const char* HomeSubPage::name() const { return headerName_.empty() ? label(section) : headerName_.c_str(); }

void HomeSubPage::onEnter() {
  SubPage::onEnter();
  selected = -1;
  headerName_.clear();
  selectedBookPath_.clear();
  transcriptionPending_ = false;
  transcriptionLastRefreshMs_ = 0;
  transcriptionDots_ = 1;
  transcriptionCachePath_.clear();
  transcriptionRecord_ = {};
  dictionarySelected_ = -1;
  dictionaryLines_.clear();
  lookupShowing_ = false;
  lookupLoading_ = false;
  lookupDefinition_.clear();
  lookupLines_.clear();
  lookupScrollLine_ = 0;
  lookupMaxScrollLine_ = 0;
  lookupNextLine_ = 0;
  lookupHasDefinition_ = false;
  lookupAlreadySaved_ = false;
  lookupSaveX_ = -1;
  lookupNextX_ = -1;
  load();
  if (section == Section::Dictionary && !lookupWord_.empty()) {
    lookupWord_ = trimText(lookupWord_);
    headerName_ = renderer.text.truncate(MONTSERRAT_16_FONT_ID, lookupWord_.c_str(),
                                         renderer.getScreenWidth() - 100, EpdFontFamily::BOLD);
    lookupShowing_ = true;
    lookupLoading_ = true;
    updateRequired = true;
  }
}

void HomeSubPage::loop() {
  if (lookupShowing_) {
    if (lookupLoading_) {
      renderPage();
      lookupLoading_ = false;
      performDictionaryLookup();
      return;
    }
    if (dictionaryLookupInput()) return;
    renderPage();
    return;
  }
  if (selected >= 0) {
    if (transcriptionPending_) {
      pollNoteTranscription();
      if (transcriptionPending_ && millis() - transcriptionLastRefreshMs_ >= 350) {
        transcriptionLastRefreshMs_ = millis();
        transcriptionDots_ = transcriptionDots_ >= 3 ? 1 : transcriptionDots_ + 1;
        updateRequired = true;
      }
      renderPage();
      return;
    }
    if (contentInput()) return;
    renderPage();
    return;
  }

  if (!selectedBookPath_.empty() && mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    selectedBookPath_.clear();
    page = 0;
    makePages();
    updateRequired = true;
    return;
  }
  if (closeInput()) return;

  if (section == Section::Dictionary && mappedInput.hasTouch()) {
    float tapNx = 0.0f;
    float tapNy = 0.0f;
    if (mappedInput.wasTouchTapInScreen(renderer, tapNx, tapNy)) {
      const int x = static_cast<int>(tapNx * renderer.getScreenWidth());
      const int y = static_cast<int>(tapNy * renderer.getScreenHeight());
      const ButtonBounds lookup = dictionaryLookupButtonBounds(renderer);
      if (x >= lookup.x && x < lookup.x + lookup.width && y >= lookup.y && y < lookup.y + lookup.height) {
        openDictionaryLookupKeyboard();
        return;
      }
      mappedInput.restoreTouchTapInScreen(renderer, tapNx, tapNy);
    }
  }

  if (mappedInput.hasTouch() && mappedInput.wasTouchSwipeLeft() && page + 1 < static_cast<int>(pages.size())) {
    ++page;
    updateRequired = true;
  } else if (mappedInput.hasTouch() && mappedInput.wasTouchSwipeRight() && page > 0) {
    --page;
    updateRequired = true;
  } else if (mappedInput.hasTouch()) {
    float tapX = 0.0f;
    float tapY = 0.0f;
    if (mappedInput.wasTouchTapInScreen(renderer, tapX, tapY)) {
      const int x = static_cast<int>(tapX * renderer.getScreenWidth());
      const int y = static_cast<int>(tapY * renderer.getScreenHeight());
      if (!selectedBookPath_.empty() && y >= contentTop && y < contentTop + bookGroupHeaderHeight) {
        selectedBookPath_.clear();
        page = 0;
        makePages();
        updateRequired = true;
        return;
      }
      const int start = pages.empty() ? 0 : pages[static_cast<size_t>(page)];
      const int end = page + 1 < static_cast<int>(pages.size()) ? pages[static_cast<size_t>(page + 1)]
                                                                  : static_cast<int>(rows.size());
      int index = -1;
      int rowY = listTop();
      for (int candidate = start; candidate < end; ++candidate) {
        const Row& candidateRow = rows[static_cast<size_t>(candidate)];
        if (showingBookList() && !candidateRow.groupStart) continue;
        if (!showingBookList() && candidateRow.cachePath != selectedBookPath_) continue;
        if (y >= rowY && y < rowY + rowHeight) {
          index = candidate;
          break;
        }
        rowY += rowHeight;
      }
      if (index >= start && index < end) {
        const Row& row = rows[static_cast<size_t>(index)];
        if (showingBookList()) {
          selectedBookPath_ = row.cachePath;
          page = 0;
          makePages();
          updateRequired = true;
          return;
        }
        const int deleteX = renderer.getScreenWidth() - 20 - deleteSize;
        if (x >= deleteX - deleteHitPadding && x < deleteX + deleteSize + deleteHitPadding) {
          remove(index);
        } else if (row.favorite && !row.bookPath.empty()) {
          openReaderFromCallback(row.bookPath, [] { onGoToHome(); });
        } else {
          selected = index;
          if (section == Section::Dictionary) {
            headerName_ = renderer.text.truncate(MONTSERRAT_16_FONT_ID, row.label.c_str(),
                                                 renderer.getScreenWidth() - 100, EpdFontFamily::BOLD);
          }
          updateRequired = true;
        }
      }
    }
  }
  renderPage();
}

void HomeSubPage::load() {
  rows.clear();
  pages.clear();
  page = 0;

  if (section == Section::Dictionary) {
    const int savedCount = SAVED_WORDS.count();
    rows.reserve(static_cast<size_t>(savedCount));
    for (int index = 0; index < savedCount; ++index) {
      Row row;
      row.label = SAVED_WORDS.wordAt(index);
      row.title = row.label;
      row.dictionaryIndex = index;
      row.groupStart = true;
      rows.push_back(std::move(row));
    }
    makePages();
    return;
  }

  if (section == Section::Favorites) {
    for (const BookState::Book& book : BOOK_STATE.getFavoriteBooks()) {
      Row row;
      row.label = book.title.empty() ? book.path : book.title;
      row.title = row.label;
      row.favorite = true;
      row.bookPath = book.path;
      rows.push_back(std::move(row));
    }
    makePages();
    return;
  }

  FsFile root = SdMan.open("/.metadata/epub");
  if (!root || !root.isDirectory()) {
    if (root) root.close();
    makePages();
    return;
  }

  int scanned = 0;
  char name[96] = {};
  root.rewindDirectory();
  while (true) {
    FsFile entry = root.openNextFile();
    if (!entry) break;
    const bool directory = entry.isDirectory();
    if (directory) entry.getName(name, sizeof(name));
    entry.close();
    if (!directory) continue;

    const std::string cachePath = std::string("/.metadata/epub/") + name;
    if (section == Section::Bookmarks) {
      loadBookmarks(cachePath, cachedTitle(cachePath));
    } else if (section == Section::Highlights && SdMan.exists((cachePath + "/ann").c_str())) {
      loadHighlights(cachePath, cachedTitle(cachePath));
    }

    if ((++scanned & 7) == 0) {
      yield();
    }
  }
  root.close();

  std::stable_sort(rows.begin(), rows.end(), [](const Row& left, const Row& right) {
    if (left.cachePath != right.cachePath) return left.cachePath < right.cachePath;
    if (left.spine != right.spine) return left.spine < right.spine;
    return left.page < right.page;
  });
  for (size_t index = 0; index < rows.size(); ++index) {
    rows[index].groupStart = index == 0 || rows[index].cachePath != rows[index - 1].cachePath;
  }
  makePages();
}

void HomeSubPage::loadBookmarks(const std::string& cachePath, const std::string& title) {
  EpubBookmarks bookmarks;
  bookmarks.load(cachePath);
  const std::string bookPath = bookPathForCachePath(cachePath);
  for (const EpubBookmark& bookmark : bookmarks.entries()) {
    Row row;
    row.label = bookmarkLabel(title, bookmark);
    row.title = title;
    row.cachePath = cachePath;
    row.spine = static_cast<int>(bookmark.spineIndex);
    row.page = static_cast<int>(bookmark.pageNumber);
    row.bookPath = bookPath;
    rows.push_back(std::move(row));
  }
}

void HomeSubPage::loadHighlights(const std::string& cachePath, const std::string& title) {
  const std::string directory = cachePath + "/ann";
  if (!SdMan.exists(directory.c_str())) return;
  const std::string bookPath = bookPathForCachePath(cachePath);

  for (const String& file : SdMan.listFiles(directory.c_str())) {
    int spine = 0;
    int page = 0;
    if (std::sscanf(file.c_str(), "s_%d_p_%d.bin", &spine, &page) != 2) continue;

    std::vector<EpubAnnotationRecord> stored;
    if (!EpubAnnotationStorage::load(cachePath, spine, page, stored)) continue;
    for (const EpubAnnotationRecord& record : stored) {
      const std::string text = trimText(record.text);
      Row row;
      row.label = text.empty() ? title : text;
      row.title = title;
      row.cachePath = cachePath;
      row.spine = spine;
      row.page = page;
      row.annotation = record;
      row.highlight = true;
      row.bookPath = bookPath;
      rows.push_back(std::move(row));
    }
  }
}

bool HomeSubPage::remove(const int index) {
  if (index < 0 || index >= static_cast<int>(rows.size())) {
    return false;
  }

  const Row& row = rows[static_cast<size_t>(index)];
  bool removed = false;
  if (row.favorite) {
    BOOK_STATE.toggleFavorite(row.bookPath, row.title);
    removed = true;
  } else if (section == Section::Dictionary) {
    removed = SAVED_WORDS.remove(row.label);
  } else if (row.highlight) {
    removed = EpubAnnotationStorage::remove(row.cachePath, row.annotation);
  } else {
    EpubBookmarks bookmarks;
    bookmarks.load(row.cachePath);
    const auto& entries = bookmarks.entries();
    const auto found = std::find_if(entries.begin(), entries.end(), [&](const EpubBookmark& bookmark) {
      return bookmark.spineIndex == row.spine && bookmark.pageNumber == row.page;
    });
    if (found != entries.end()) {
      removed = bookmarks.remove(row.cachePath, static_cast<size_t>(found - entries.begin()));
    }
  }

  if (!removed) {
    return false;
  }
  load();
  if (!selectedBookPath_.empty()) {
    const bool bookStillPresent = std::any_of(rows.begin(), rows.end(), [this](const Row& candidate) {
      return candidate.cachePath == selectedBookPath_;
    });
    if (!bookStillPresent) {
      selectedBookPath_.clear();
      page = 0;
      makePages();
    }
  }
  updateRequired = true;
  return true;
}

bool HomeSubPage::contentInput() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    selected = -1;
    headerName_.clear();
    updateRequired = true;
    return true;
  }
  if (!mappedInput.hasTouch()) {
    return false;
  }
  if (mappedInput.wasTouchSwipeUp()) {
    selected = -1;
    updateRequired = true;
    return true;
  }

  float tapX = 0.0f;
  float tapY = 0.0f;
  if (!mappedInput.wasTouchTapInScreen(renderer, tapX, tapY)) {
    return false;
  }
  const int x = static_cast<int>(tapX * renderer.getScreenWidth());
  const int y = static_cast<int>(tapY * renderer.getScreenHeight());
  if (x >= renderer.getScreenWidth() - 60 && x < renderer.getScreenWidth() - 20 && y >= 20 && y < 60) {
    selected = -1;
    headerName_.clear();
    updateRequired = true;
    return true;
  }
  if ((section == Section::Bookmarks || section == Section::Highlights) && selected >= 0 &&
      selected < static_cast<int>(rows.size())) {
    const Row& row = rows[static_cast<size_t>(selected)];
    const ButtonBounds openPage = openPageActionBounds(renderer);
    if (!row.bookPath.empty() && row.spine >= 0 && row.page >= 0 && x >= openPage.x && x < openPage.x + openPage.width &&
        y >= openPage.y && y < openPage.y + openPage.height) {
      openReaderFromCallback(row.bookPath, [] { onGoToHome(); }, row.spine, row.page);
      return true;
    }
  }
  if (section == Section::Highlights && selected >= 0 && selected < static_cast<int>(rows.size())) {
    const Row& row = rows[static_cast<size_t>(selected)];
    if (!row.annotation.noteAudioPath.empty() && row.annotation.note.empty()) {
      const int buttonY = renderer.getScreenHeight() - contentBottom - Button::height;
      const int buttonW = Button::width(renderer, "Transcribe note", systemFontId());
      const ButtonBounds openPage = openPageActionBounds(renderer);
      const int buttonX = openPage.x - 12 - buttonW;
      const ButtonBounds button{buttonX, buttonY, buttonW, Button::height};
      if (x >= button.x && x < button.x + button.width && y >= button.y && y < button.y + button.height) {
        startNoteTranscription();
        return true;
      }
    }
  }
  return false;
}

void HomeSubPage::menu() {
  if (selected < 0) {
    SubPage::menu();
    if (section == Section::Dictionary && !lookupShowing_) {
      const ButtonBounds lookup = dictionaryLookupButtonBounds(renderer);
      Button::render(renderer, lookup, "Look up", true, systemFontId());
    }
    return;
  }
  if (section == Section::Dictionary) {
    SubPage::menu();
    return;
  }
  renderer.bitmap.icon(Close, renderer.getScreenWidth() - 60, 20, 40, 40);
  if ((section == Section::Bookmarks || section == Section::Highlights) && selected >= 0 &&
      selected < static_cast<int>(rows.size())) {
    const Row& row = rows[static_cast<size_t>(selected)];
    if (!row.bookPath.empty() && row.spine >= 0 && row.page >= 0) {
      const ButtonBounds openPage = openPageActionBounds(renderer);
      renderer.rectangle.fill(openPage.x, openPage.y, openPage.width, openPage.height, true);
      renderer.bitmap.iconScaled(Download, openPage.x + openPageActionPadding, openPage.y + openPageActionPadding,
                                 openPageIconSize, openPageIconSize, openPageIconSize, openPageIconSize,
                                 BitmapRender::Orientation::Rotate270CW, true);
    }
  }
}

bool HomeSubPage::showingBookList() const {
  return section != Section::Favorites && section != Section::Dictionary && selectedBookPath_.empty();
}

int HomeSubPage::listTop() const {
  return selectedBookPath_.empty() ? contentTop : contentTop + bookGroupHeaderHeight;
}

void HomeSubPage::makePages() {
  pages.clear();
  int y = listTop();
  const int bottom = renderer.getScreenHeight() - contentBottom -
                     (section == Section::Dictionary && selectedBookPath_.empty()
                          ? Button::height + dictionaryLookupButtonGap
                          : 0);
  for (int index = 0; index < static_cast<int>(rows.size()); ++index) {
    const Row& row = rows[static_cast<size_t>(index)];
    if (showingBookList() && !row.groupStart) continue;
    if (!showingBookList() && row.cachePath != selectedBookPath_) continue;
    if (pages.empty()) pages.push_back(index);
    const int height = rowHeight;
    if (y > listTop() && y + height > bottom) {
      pages.push_back(index);
      y = listTop();
    }
    y += height;
  }
}

void HomeSubPage::content() {
  if (lookupShowing_) {
    lookupContent();
    return;
  }
  if (selected >= 0 && selected < static_cast<int>(rows.size())) {
    const Row& row = rows[static_cast<size_t>(selected)];
    if (section == Section::Dictionary) {
      dictionaryContent(row);
      return;
    }
    if (row.highlight) {
      highlightContent(row);
      return;
    }
    if (home::preview(renderer, row.cachePath, row.spine, row.page)) {
      return;
    }
    renderer.text.centered(systemFontId(), renderer.getScreenHeight() / 2, "Preview unavailable", true,
                           EpdFontFamily::BOLD);
    return;
  }

  if (rows.empty()) {
    renderer.text.centered(systemFontId(), (contentTop + renderer.getScreenHeight()) / 2, emptyState(section));
    return;
  }

  const int font = systemFontId();
  const int bottom = renderer.getScreenHeight() - contentBottom -
                     (section == Section::Dictionary && selectedBookPath_.empty()
                          ? Button::height + dictionaryLookupButtonGap
                          : 0);
  const int start = pages.empty() ? 0 : pages[static_cast<size_t>(page)];
  const int end = page + 1 < static_cast<int>(pages.size()) ? pages[static_cast<size_t>(page + 1)]
                                                              : static_cast<int>(rows.size());
  const bool bookList = showingBookList();
  const bool noteList = !selectedBookPath_.empty();
  constexpr int left = 20;
  if (noteList) {
    std::string bookTitle = "Untitled book";
    for (const Row& row : rows) {
      if (row.cachePath == selectedBookPath_) {
        bookTitle = row.title;
        break;
      }
    }
    const int backCaretWidth = renderer.text.getWidth(font, "‹");
    const int titleLeft = left + backCaretWidth + 12;
    const std::string breadcrumbText =
        renderer.text.truncate(font, bookTitle.c_str(), renderer.getScreenWidth() - titleLeft - 20,
                               EpdFontFamily::BOLD);
    const int breadcrumbY = contentTop + (bookGroupHeaderHeight - renderer.text.getLineHeight(font)) / 2;
    renderer.text.render(font, left, breadcrumbY, "‹", true, EpdFontFamily::REGULAR);
    renderer.text.render(font, titleLeft, breadcrumbY, breadcrumbText.c_str(), true, EpdFontFamily::BOLD);
  }

  int y = listTop();
  for (int index = start; index < end; ++index) {
    const Row& row = rows[static_cast<size_t>(index)];
    if (bookList && !row.groupStart) continue;
    if (noteList && row.cachePath != selectedBookPath_) continue;
    const int rightActionX = renderer.getScreenWidth() - 20 - deleteSize;
    const int caretWidth = renderer.text.getWidth(font, "›");
    const int available = bookList ? renderer.getScreenWidth() - left - caretWidth - 30 : rightActionX - left - 14;
    const char* textSource = bookList ? row.title.c_str() : row.label.c_str();
    const std::string text = renderer.text.truncate(font, textSource, std::max(40, available), EpdFontFamily::REGULAR);
    const int textY = y + (rowHeight - renderer.text.getLineHeight(font)) / 2;
    renderer.text.render(font, left, textY, text.c_str(), true, EpdFontFamily::REGULAR);
    if (bookList) {
      renderer.text.render(font, renderer.getScreenWidth() - caretWidth - 30, textY, "›", true,
                           EpdFontFamily::REGULAR);
    } else {
      renderer.bitmap.icon(Trash, rightActionX, y + (rowHeight - deleteSize) / 2, deleteSize, deleteSize);
    }
    bool hasNextVisibleRow = false;
    for (int next = index + 1; next < end; ++next) {
      const Row& nextRow = rows[static_cast<size_t>(next)];
      if ((bookList && nextRow.groupStart) || (noteList && nextRow.cachePath == selectedBookPath_)) {
        hasNextVisibleRow = true;
        break;
      }
    }
    if (hasNextVisibleRow && y + rowHeight <= bottom) {
      renderer.line.render(0, y + rowHeight - 1, renderer.getScreenWidth(), y + rowHeight - 1, true,
                           LineRender::Style::Dotted);
    }
    y += rowHeight;
  }
}

void HomeSubPage::dictionaryContent(const Row& row) {
  constexpr int left = 20;
  constexpr int top = contentTop;
  constexpr int bottom = contentBottom;
  const int width = renderer.getScreenWidth() - left * 2;

  if (dictionarySelected_ != selected) {
    dictionaryLines_ = layoutDefinitionBlocks(renderer, parseHtmlToBlocks(SAVED_WORDS.definitionAt(row.dictionaryIndex)), width);
    dictionarySelected_ = selected;
  }
  renderStyledLines(renderer, dictionaryLines_, left, top, renderer.getScreenHeight() - bottom);
}

void HomeSubPage::performDictionaryLookup() {
  lookupLoading_ = false;
  lookupWord_ = trimText(lookupWord_);
  lookupDefinition_.clear();
  lookupLines_.clear();
  lookupScrollLine_ = 0;
  lookupHasDefinition_ = false;
  lookupAlreadySaved_ = !lookupWord_.empty() && SAVED_WORDS.contains(lookupWord_);

  bool truncated = false;
  if (lookupWord_.empty()) {
    lookupDefinition_ = "Nothing to look up.";
  } else if (READER_SETTINGS.dictionaryFolder[0] == '\0') {
    lookupDefinition_ = "No dictionary selected. Pick one in Settings > Reader > Choose dictionary.";
  } else {
    StarDictLookup dictionary;
    const std::string folder = std::string("/dictionaries/") + READER_SETTINGS.dictionaryFolder;
    if (!dictionary.open(folder)) {
      lookupDefinition_ = "Could not open the selected dictionary.";
    } else if (!dictionary.lookup(lookupWord_, lookupDefinition_, &truncated)) {
      lookupDefinition_ = "No definition found.";
    } else {
      lookupHasDefinition_ = true;
    }
  }
  if (truncated) {
    lookupDefinition_ += " \xE2\x80\xA6";
  }
  lookupLines_ = layoutDefinitionBlocks(renderer, parseHtmlToBlocks(lookupDefinition_),
                                         renderer.getScreenWidth() - 40);
  lookupShowing_ = true;
  updateRequired = true;
}

void HomeSubPage::closeDictionaryLookup() {
  lookupShowing_ = false;
  lookupLoading_ = false;
  headerName_.clear();
  lookupWord_.clear();
  lookupDefinition_.clear();
  lookupLines_.clear();
  lookupScrollLine_ = 0;
  lookupMaxScrollLine_ = 0;
  lookupNextLine_ = 0;
  lookupHasDefinition_ = false;
  lookupAlreadySaved_ = false;
  lookupSaveX_ = -1;
  lookupNextX_ = -1;
  load();
  updateRequired = true;
}

void HomeSubPage::saveDictionaryLookup() {
  if (!lookupHasDefinition_ || lookupAlreadySaved_ || lookupWord_.empty()) {
    return;
  }
  if (SAVED_WORDS.add(lookupWord_, lookupDefinition_)) {
    lookupAlreadySaved_ = true;
    updateRequired = true;
  }
}

bool HomeSubPage::dictionaryLookupInput() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    closeDictionaryLookup();
    return true;
  }
  if (mappedInput.hasTouch() && mappedInput.wasTouchSwipeUp()) {
    closeDictionaryLookup();
    return true;
  }
  if (!mappedInput.hasTouch()) {
    return false;
  }

  float tapNx = 0.0f;
  float tapNy = 0.0f;
  if (!mappedInput.wasTouchTapInScreen(renderer, tapNx, tapNy)) {
    return false;
  }
  const int x = static_cast<int>(tapNx * renderer.getScreenWidth());
  const int y = static_cast<int>(tapNy * renderer.getScreenHeight());
  if (x >= renderer.getScreenWidth() - 60 && x < renderer.getScreenWidth() - 20 && y >= 20 && y < 60) {
    closeDictionaryLookup();
    return true;
  }
  if (x >= lookupSaveX_ && x < lookupSaveX_ + lookupSaveW_ && y >= lookupSaveY_ && y < lookupSaveY_ + lookupSaveH_) {
    saveDictionaryLookup();
    return true;
  }
  if (x >= lookupNextX_ && x < lookupNextX_ + lookupNextW_ && y >= lookupNextY_ && y < lookupNextY_ + lookupNextH_) {
    if (lookupScrollLine_ < lookupMaxScrollLine_) {
      lookupScrollLine_ = std::min(lookupMaxScrollLine_, std::max(lookupScrollLine_ + 1, lookupNextLine_));
      updateRequired = true;
    }
    return true;
  }
  mappedInput.restoreTouchTapInScreen(renderer, tapNx, tapNy);
  return false;
}

void HomeSubPage::lookupContent() {
  constexpr int left = 20;
  constexpr int top = contentTop;
  constexpr int bottom = contentBottom;
  constexpr int buttonGap = 12;
  const int definitionTop = top;
  const int definitionBottom = renderer.getScreenHeight() - bottom;
  const int buttonFont = systemFontId();
  if (lookupLoading_) {
    renderer.text.centered(systemFontId(), renderer.getScreenHeight() / 2, "Looking up...", true);
    return;
  }
  const char* saveLabel = lookupAlreadySaved_ ? "Saved" : "Save";
  const int saveW = Button::width(renderer, saveLabel, buttonFont);
  const int buttonH = Button::height;
  const int saveY = definitionBottom - buttonH;
  const int textBottom = saveY - buttonGap;

  auto maxScrollLineFor = [&](const int availableH) {
    int heightFromEnd = 0;
    int index = static_cast<int>(lookupLines_.size()) - 1;
    while (index >= 0) {
      const DefinitionStyledLine& line = lookupLines_[static_cast<size_t>(index)];
      const int lineHeight = renderer.text.getLineHeight(line.fontId) + line.extraGapBeforePx;
      if (heightFromEnd + lineHeight > availableH) break;
      heightFromEnd += lineHeight;
      --index;
    }
    return static_cast<size_t>(index + 1);
  };

  lookupMaxScrollLine_ = maxScrollLineFor(textBottom - definitionTop);
  lookupScrollLine_ = std::min(lookupScrollLine_, lookupMaxScrollLine_);
  lookupSaveW_ = saveW;
  lookupSaveH_ = buttonH;
  lookupSaveY_ = saveY;
  lookupNextX_ = -1;
  lookupNextY_ = -1;
  lookupNextW_ = 0;
  lookupNextH_ = 0;

  if (lookupMaxScrollLine_ > 0) {
    lookupNextW_ = Button::width(renderer, "Next", buttonFont);
    lookupNextH_ = buttonH;
    lookupNextX_ = renderer.getScreenWidth() - 20 - lookupNextW_;
    lookupNextY_ = saveY;
    lookupSaveX_ = lookupNextX_ - buttonGap - lookupSaveW_;
  } else {
    lookupSaveX_ = renderer.getScreenWidth() - 20 - lookupSaveW_;
  }

  int usedHeight = 0;
  size_t nextLine = lookupScrollLine_;
  while (nextLine < lookupLines_.size()) {
    const DefinitionStyledLine& line = lookupLines_[nextLine];
    const int lineHeight = renderer.text.getLineHeight(line.fontId) + line.extraGapBeforePx;
    if (usedHeight + lineHeight > textBottom - definitionTop) break;
    usedHeight += lineHeight;
    ++nextLine;
  }
  lookupNextLine_ = nextLine;
  renderStyledLines(renderer, lookupLines_, left, definitionTop, textBottom, lookupScrollLine_);
  Button::render(renderer, {lookupSaveX_, lookupSaveY_, lookupSaveW_, lookupSaveH_}, saveLabel, false, buttonFont);
  if (lookupScrollLine_ < lookupMaxScrollLine_) {
    Button::render(renderer, {lookupNextX_, lookupNextY_, lookupNextW_, lookupNextH_}, "Next", true, buttonFont);
  }
}

void HomeSubPage::highlightContent(const Row& row) {
  const int font = systemFontId();
  constexpr int left = 20;
  constexpr int right = 20;
  const int width = renderer.getScreenWidth() - left - right;
  int y = contentTop;
  const int bottom = renderer.getScreenHeight() - contentBottom;
  const int lineHeight = renderer.text.getLineHeight(font);
  const bool hasNote = !row.annotation.note.empty();
  const bool hasVoiceNote = !row.annotation.noteAudioPath.empty() && !hasNote;
  const int noteButtonY = bottom - Button::height;
  const int noteLabelY = noteButtonY - lineHeight - 10;
  const int noteReserve = hasNote ? lineHeight * 3 + 20 : 0;
  const int contentBottomForActions = bottom - openPageActionSize - 10;
  const int textBottom = hasVoiceNote ? noteLabelY - 14 : contentBottomForActions - noteReserve;

  const std::string title = renderer.text.truncate(font, row.title.c_str(), width, EpdFontFamily::BOLD);
  renderer.text.render(font, left, y, title.c_str(), true, EpdFontFamily::BOLD);
  y += renderer.text.getLineHeight(font) + 20;

  std::string remaining = row.annotation.text.empty() ? row.label : row.annotation.text;
  while (!remaining.empty() && y + lineHeight <= textBottom) {
    size_t length = remaining.size();
    while (length > 0 && renderer.text.getWidth(font, remaining.substr(0, length).c_str()) > width) {
      const size_t space = remaining.rfind(' ', length - 1);
      length = space == std::string::npos ? length - 1 : space;
    }
    if (length == 0) break;
    const std::string line = trimText(remaining.substr(0, length));
    renderer.text.render(font, left, y, line.c_str(), true);
    remaining.erase(0, length);
    remaining = trimText(remaining);
    y += lineHeight + 6;
  }
  if (hasNote && y + lineHeight <= contentBottomForActions) {
    y += 10;
    renderer.text.render(font, left, y, "Note", true, EpdFontFamily::BOLD);
    y += lineHeight + 6;

    std::string note = row.annotation.note;
    while (!note.empty() && y + lineHeight <= contentBottomForActions) {
      size_t length = note.size();
      while (length > 0 && renderer.text.getWidth(font, note.substr(0, length).c_str()) > width) {
        const size_t space = note.rfind(' ', length - 1);
        length = space == std::string::npos ? length - 1 : space;
      }
      if (length == 0) break;
      const std::string line = trimText(note.substr(0, length));
      renderer.text.render(font, left, y, line.c_str(), true);
      note.erase(0, length);
      note = trimText(note);
      y += lineHeight + 6;
    }
  }
  if (hasVoiceNote) {
    const char* buttonLabel = "Transcribe note";
    const int buttonW = Button::width(renderer, "Transcribe note", font);
    const ButtonBounds openPage = openPageActionBounds(renderer);
    const int buttonX = openPage.x - 12 - buttonW;
    Button::render(renderer, {buttonX, noteButtonY, buttonW, Button::height}, transcriptionPending_ ? "" : buttonLabel,
                   true, font);
    if (transcriptionPending_) {
      constexpr int dotSize = 6;
      constexpr int dotGap = 8;
      const int dotCount = transcriptionDots_;
      const int dotsWidth = dotCount * dotSize + (dotCount - 1) * dotGap;
      const int dotsX = buttonX + (buttonW - dotsWidth) / 2;
      const int dotsY = noteButtonY + (Button::height - dotSize) / 2;
      for (int dot = 0; dot < dotCount; ++dot) {
        renderer.rectangle.fill(dotsX + dot * (dotSize + dotGap), dotsY, dotSize, dotSize, false);
      }
    }
  }
}

void HomeSubPage::startNoteTranscription() {
  if (transcriptionPending_ || selected < 0 || selected >= static_cast<int>(rows.size())) {
    return;
  }
  const Row& row = rows[static_cast<size_t>(selected)];
  if (row.annotation.noteAudioPath.empty()) {
    return;
  }
  if (!GeminiTranscription::start(row.annotation.noteAudioPath)) {
    updateRequired = true;
    return;
  }
  transcriptionCachePath_ = row.cachePath;
  transcriptionRecord_ = row.annotation;
  transcriptionPending_ = true;
  transcriptionLastRefreshMs_ = millis();
  transcriptionDots_ = 1;
  updateRequired = true;
}

void HomeSubPage::pollNoteTranscription() {
  const GeminiTranscription::Result result = GeminiTranscription::poll();
  if (!result.finished) {
    return;
  }
  transcriptionPending_ = false;
  if (!result.success || result.transcript.empty()) {
    updateRequired = true;
    return;
  }
  EpubAnnotationRecord updated = transcriptionRecord_;
  updated.note = result.transcript;
  if (!EpubAnnotationStorage::update(transcriptionCachePath_, transcriptionRecord_, updated)) {
    updateRequired = true;
    return;
  }
  rows[static_cast<size_t>(selected)].annotation = updated;
  transcriptionRecord_ = updated;
  updateRequired = true;
}
