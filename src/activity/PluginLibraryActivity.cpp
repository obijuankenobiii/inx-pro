#include "PluginLibraryActivity.h"

#include <ArduinoJson.h>
#include <GfxRenderer.h>

#include <algorithm>

#include "activity/page/components/global/Button.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "system/PluginManager.h"

extern void openReaderFromCallback(const std::string& path, std::function<void()> returnToCaller);
extern void onGoToSeries();

namespace {
constexpr int kHeaderHeight = 70;
constexpr int kRowHeight = 58;
constexpr int kLeft = 24;
constexpr int kRight = 24;
constexpr int kTop = 92;
}

PluginLibraryActivity::PluginLibraryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                             std::string pluginId, std::string function, std::string title,
                                             std::function<void()> onBack)
    : Activity(std::move(title), renderer, mappedInput),
      pluginId_(std::move(pluginId)),
      function_(std::move(function)),
      title_(name),
      onBack_(std::move(onBack)) {}

void PluginLibraryActivity::onEnter() {
  Activity::onEnter();
  scroll_ = 0;
  load();
  render();
}

void PluginLibraryActivity::load() {
  books_.clear();
  visiblePaths_.clear();
  error_ = false;
  errorMessage_.clear();
  std::string output;
  std::string error;
  if (!PluginManager::invokeString(pluginId_.c_str(), function_.c_str(), nullptr, output, error)) {
    error_ = true;
    errorMessage_ = error.empty() ? "Could not load plugin data" : error;
    return;
  }

  JsonDocument document;
  if (deserializeJson(document, output) != DeserializationError::Ok || !document.is<JsonArray>()) {
    error_ = true;
    errorMessage_ = "Plugin returned invalid library data";
    return;
  }
  for (JsonObject item : document.as<JsonArray>()) {
    const char* path = item["path"] | "";
    if (!path || !path[0]) continue;
    Book book;
    book.path = path;
    book.title = item["title"] | book.path;
    book.author = item["author"] | "";
    book.series = item["series"] | "";
    book.order = item["order"] | 0;
    books_.push_back(std::move(book));
  }
  std::stable_sort(books_.begin(), books_.end(), [](const Book& left, const Book& right) {
    if (left.series != right.series) return left.series < right.series;
    if (left.order != right.order) return left.order < right.order;
    return left.title < right.title;
  });
}

void PluginLibraryActivity::render() {
  renderer.clearScreen();
  renderer.line.render(0, kHeaderHeight, renderer.getScreenWidth(), kHeaderHeight, true);
  renderer.text.render(systemFontId(), kLeft, 24, title_.c_str(), true, EpdFontFamily::BOLD);
  const int font = systemFontId();
  const int lineHeight = renderer.text.getLineHeight(font);
  const int maxRows = std::max(1, (renderer.getScreenHeight() - kTop - 70) / kRowHeight);
  const int start = std::min(scroll_, static_cast<int>(books_.size()));
  const int end = std::min(static_cast<int>(books_.size()), start + maxRows);
  visiblePaths_.clear();

  if (error_) {
    renderer.text.centered(font, renderer.getScreenHeight() / 2, errorMessage_.c_str(), true);
  } else if (books_.empty()) {
    renderer.text.centered(font, renderer.getScreenHeight() / 2, "No books have been added to a series.");
  } else {
    std::string previousSeries;
    int row = 0;
    for (int index = start; index < end; ++index) {
      const Book& book = books_[static_cast<size_t>(index)];
      const int y = kTop + row * kRowHeight;
      const bool firstInSeries = book.series != previousSeries;
      const int bookY = firstInSeries ? y + lineHeight + 2 : y;
      if (firstInSeries) {
        renderer.text.render(font, kLeft, y, book.series.c_str(), true, EpdFontFamily::BOLD);
        previousSeries = book.series;
      }
      const std::string order = std::to_string(book.order) + ".";
      renderer.text.render(font, kLeft, bookY, order.c_str(), true);
      const int titleX = kLeft + 38;
      renderer.text.render(font, titleX, bookY, book.title.c_str(), true);
      if (!book.author.empty()) {
        renderer.text.render(MONTSERRAT_8_FONT_ID, titleX, bookY + lineHeight + 2, book.author.c_str(), true);
      }
      visiblePaths_.push_back(book.path);
      renderer.line.render(kLeft, y + kRowHeight - 8, renderer.getScreenWidth() - kRight, y + kRowHeight - 8, true);
      ++row;
    }
  }
  mappedInput.mapLabels("\xC2\xAB Back", books_.empty() ? "" : "Open", "", "");
  renderer.displayBuffer();
}

void PluginLibraryActivity::openBook(const std::string& path) {
  if (path.empty()) return;
  openReaderFromCallback(path, [] { onGoToSeries(); });
}

void PluginLibraryActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (onBack_) onBack_();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    scroll_ = std::max(0, scroll_ - 1);
    render();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    scroll_ = std::min(std::max(0, static_cast<int>(books_.size()) - 1), scroll_ + 1);
    render();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    for (const std::string& path : visiblePaths_) {
      if (!path.empty()) {
        openBook(path);
        return;
      }
    }
  }
  if (!mappedInput.hasTouch()) return;
  float nx = 0.0f;
  float ny = 0.0f;
  if (!mappedInput.wasTouchTapInScreen(renderer, nx, ny)) return;
  const int y = static_cast<int>(ny * renderer.getScreenHeight());
  if (y < kTop) return;
  const int row = (y - kTop) / kRowHeight;
  if (row < 0 || row >= static_cast<int>(visiblePaths_.size())) return;
  openBook(visiblePaths_[static_cast<size_t>(row)]);
}
