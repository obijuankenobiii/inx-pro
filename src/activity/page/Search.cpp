#include "Search.h"

#include <GfxRenderer.h>

#include "components/search/SearchText.h"
#include "images/BookSmall.h"
#include "images/Folder.h"
#include "images/Refresh.h"
#include "images/Shift.h"
#include "state/SystemSetting.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"

extern void onGoToHome();
extern void onGoToLibrary(const std::string& path);
extern void onSelectBook(const std::string& path);

namespace {
constexpr int kKeyboardControlHeight = 44;
constexpr int kKeyboardControlMargin = 6;

void renderKeyboardControl(const GfxRenderer& renderer, const int y, const int height,
                           const BitmapRender::Orientation orientation) {
  const int width = renderer.getScreenWidth() - kKeyboardControlMargin * 2;
  renderer.rectangle.render(kKeyboardControlMargin, y, std::max(1, width), height, true);
  constexpr int iconSize = 30;
  renderer.bitmap.icon(Shift, (renderer.getScreenWidth() - iconSize) / 2, y + (height - iconSize) / 2, iconSize,
                       iconSize, orientation);
}
}  // namespace

Search::Search(GfxRenderer& renderer, MappedInputManager& mappedInput, std::function<void()> returnToCaller)
    : SubPage("Search", renderer, mappedInput,
              [returnToCaller = std::move(returnToCaller)]() mutable {
                if (returnToCaller) {
                  returnToCaller();
                } else {
                  onGoToHome();
                }
              }) {}

void Search::onEnter() {
  Page::onEnter();
  query.clear();
  results.clear();
  scroll = 0;
  keyboardCollapsed_ = false;
}

int Search::resultHeight() const {
  return SETTINGS.keyboardLayout == SystemSetting::KEYBOARD_NUMPAD ? 56 : 60;
}

int Search::keyboardHeight() const { return keyboard.height(renderer); }

int Search::keyboardControlHeight() const { return kKeyboardControlHeight; }

int Search::keyboardBottom() const {
  return keyboardCollapsed_ ? renderer.getScreenHeight() - keyboardControlHeight() : renderer.getScreenHeight();
}

int Search::keyboardTop() const {
  return keyboardCollapsed_ ? keyboardBottom()
                            : keyboardBottom() - keyboardHeight();
}

int Search::refreshX() const { return renderer.getScreenWidth() - 20 - 40; }

int Search::refreshY() const { return SearchText::top() + (SearchText::height - 40) / 2; }

int Search::resultsTop() const { return SearchText::top() + SearchText::height + 10; }

int Search::maxScroll() const {
  const int visible = std::max(0, keyboardTop() - resultsTop());
  const int total = static_cast<int>(results.size()) * resultHeight();
  return std::max(0, total - visible);
}

void Search::updateResults() {
  if (query.empty()) {
    results.clear();
  } else {
    LibraryIndex::search(query, results, 24);
  }
  scroll = 0;
  updateRequired = true;
}

void Search::loop() {
  if (closeInput(false)) return;

  if (isOpen()) {
    renderPage();
    return;
  }

  if (mappedInput.hasTouch()) {
    float swipeNx = 0.0f;
    float swipeNy = 0.0f;
    if (mappedInput.wasTouchSwipeUpInScreen(renderer, swipeNx, swipeNy)) {
      const int startY = static_cast<int>(swipeNy * renderer.getScreenHeight());
      if (startY >= keyboardTop()) {
        dismiss();
        return;
      }

      if (!query.empty()) {
        scroll = std::min(maxScroll(), scroll + resultHeight());
        updateRequired = true;
        return;
      }
    }

    if (!query.empty() && mappedInput.wasTouchSwipeDown()) {
      scroll = std::max(0, std::min(maxScroll(), scroll) - resultHeight());
      updateRequired = true;
      return;
    }

    float tapNx = 0.0f;
    float tapNy = 0.0f;
    if (mappedInput.wasTouchTapInScreen(renderer, tapNx, tapNy)) {
      const int tapX = static_cast<int>(tapNx * renderer.getScreenWidth());
      const int tapY = static_cast<int>(tapNy * renderer.getScreenHeight());

      if (keyboardCollapsed_ && tapY >= renderer.getScreenHeight() - keyboardControlHeight()) {
        keyboardCollapsed_ = !keyboardCollapsed_;
        scroll = std::min(scroll, maxScroll());
        updateRequired = true;
        return;
      }

      if (tapX >= refreshX() && tapX < refreshX() + 40 && tapY >= refreshY() && tapY < refreshY() + 40) {
        refresh();
        return;
      }

      if (!keyboardCollapsed_ && keyboard.tap(renderer, keyboardTop(), keyboardBottom(), tapX, tapY, query)) {
        if (keyboard.consumeCollapse()) {
          keyboardCollapsed_ = true;
          scroll = std::min(scroll, maxScroll());
          updateRequired = true;
          return;
        }
        updateResults();
        if (keyboard.consumeGo() && !results.empty()) {
          const LibraryIndex::Book& result = results.front();
          if (result.type == LibraryIndex::Book::Type::FOLDER) {
            onGoToLibrary(result.path);
          } else {
            onSelectBook(result.path);
          }
        }
        return;
      }

      const int first = resultsTop();
      const int last = keyboardTop();
      if (tapY >= first && tapY < last) {
        const int currentScroll = std::min(maxScroll(), std::max(0, scroll));
        const int row = (tapY - first + currentScroll) / resultHeight();
        const int rowY = first + row * resultHeight() - currentScroll;
        if (row >= 0 && row < static_cast<int>(results.size()) && rowY >= first &&
            rowY + resultHeight() <= last) {
          const LibraryIndex::Book& result = results[static_cast<size_t>(row)];
          if (result.type == LibraryIndex::Book::Type::FOLDER) {
            onGoToLibrary(result.path);
          } else {
            onSelectBook(result.path);
          }
          return;
        }
      }

      return;
    }
  }

  renderPage();
}

void Search::content() {
  SearchText::render(renderer, query);

  if (!query.empty()) {
    drawResults();
  }

  if (!keyboardCollapsed_) {
    keyboard.render(renderer, keyboardTop(), keyboardBottom());
  } else {
    renderKeyboardControl(renderer, renderer.getScreenHeight() - keyboardControlHeight(), keyboardControlHeight(),
                           BitmapRender::Orientation::None);
  }
  renderer.bitmap.icon(Refresh, refreshX(), refreshY(), 40, 40);
}

void Search::drawResults() const {
  const int top = resultsTop();
  const int width = renderer.getScreenWidth();
  const int font = systemFontId();

  if (results.empty()) {
    if (!query.empty()) {
      const char* message = LibraryIndex::hasIndex() ? "No matching books" : "Build the library index first";
      renderer.text.centered(font, top + 18, message);
    }
    return;
  }

  const int rowHeight = resultHeight();
  const int bottom = keyboardTop();
  const int currentScroll = std::min(maxScroll(), std::max(0, scroll));
  constexpr int margin = 10;
  for (int i = 0; i < static_cast<int>(results.size()); ++i) {
    const int y = top + i * rowHeight - currentScroll;
    if (y < top || y + rowHeight > bottom) continue;
    const LibraryIndex::Book& result = results[static_cast<size_t>(i)];
    constexpr int iconSize = 24;
    const int iconX = 15 + margin;
    const int iconY = y + (rowHeight - iconSize) / 2;
    if (result.type == LibraryIndex::Book::Type::FOLDER) {
      renderer.bitmap.icon(Folder, iconX, iconY, iconSize, iconSize);
    } else {
      renderer.bitmap.icon(BookSmall, iconX, iconY + 2, iconSize, iconSize);
    }

    constexpr int textX = 55 + margin;
    const int available = std::max(40, width - textX - 20 - margin);
    const std::string title = renderer.text.truncate(font, result.title.c_str(), available);
    const int textY = y + (rowHeight - renderer.text.getLineHeight(font)) / 2;
    renderer.text.render(font, textX, textY, title.c_str(), true);
    renderer.line.render(margin, y + rowHeight - 1, width - margin, y + rowHeight - 1, true,
                         LineRender::Style::Dotted);
  }
}
