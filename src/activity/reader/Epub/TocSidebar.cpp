#include "TocSidebar.h"

#include <Epub.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>

#include <algorithm>
#include <string>
#include <utility>

#include "images/Close.h"
#include "images/Koreader.h"
#include "activity/page/components/global/Button.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"

namespace {
constexpr int padding = 20;
constexpr int closeSize = 24;
constexpr int closeHitSize = 40;
constexpr int dividerWidth = 1;
constexpr int footerPadding = 0;
constexpr int syncIconSize = 40;
}  // namespace

TocSidebar::TocSidebar(GfxRenderer& renderer, Select onSelect, Dismiss onDismiss, Sync onSync)
    : renderer(renderer), onSelect(std::move(onSelect)), onDismiss(std::move(onDismiss)), onSync(std::move(onSync)) {}

int TocSidebar::width() const {
  const int screenWidth = renderer.getScreenWidth();
  return std::min(screenWidth - 40, std::max(240, screenWidth * 2 / 3));
}

int TocSidebar::headerHeight() const { return rowHeight() + 8; }

int TocSidebar::footerHeight() const { return Button::height + footerPadding * 2; }

int TocSidebar::rowHeight() const { return 66; }

int TocSidebar::visibleRows() const {
  return std::max(1, (renderer.getScreenHeight() - headerHeight() - footerHeight()) / rowHeight());
}

ButtonBounds TocSidebar::syncButtonBounds() const {
  const int footerY = renderer.getScreenHeight() - footerHeight();
  return {0, footerY + footerPadding, width(), Button::height};
}

int TocSidebar::maxScroll() const {
  return std::max(0, static_cast<int>(visibleItems.size()) - visibleRows());
}

bool TocSidebar::hasChildren(const int tocIndex) const {
  return tocIndex >= 0 && tocIndex + 1 < static_cast<int>(levels.size()) && levels[tocIndex + 1] > levels[tocIndex];
}

void TocSidebar::rows() {
  visibleItems.clear();

  int hiddenLevel = -1;
  for (int tocIndex = 0; tocIndex < static_cast<int>(levels.size()); ++tocIndex) {
    const int level = levels[tocIndex];
    if (hiddenLevel >= 0 && level > hiddenLevel) continue;
    hiddenLevel = -1;

    visibleItems.push_back(tocIndex);
    if (collapsed[tocIndex] && hasChildren(tocIndex)) hiddenLevel = level;
  }

  scroll = std::min(scroll, maxScroll());
}

void TocSidebar::show(Epub* book, const int spineIndex) {
  epub = book;
  levels.clear();
  collapsed.clear();
  visibleItems.clear();

  const int count = epub ? epub->getTocItemsCount() : 0;
  levels.reserve(count);
  collapsed.assign(count, 0);
  for (int tocIndex = 0; tocIndex < count; ++tocIndex) {
    levels.push_back(epub->getTocItem(tocIndex).level);
  }
  for (int tocIndex = 0; tocIndex < count; ++tocIndex) {
    collapsed[tocIndex] = hasChildren(tocIndex);
  }

  currentTocIndex = epub ? epub->getTocIndexForSpineIndex(spineIndex) : -1;
  if (currentTocIndex >= 0 && currentTocIndex < count) {
    int childLevel = levels[static_cast<size_t>(currentTocIndex)];
    for (int tocIndex = currentTocIndex - 1; tocIndex >= 0 && childLevel > 0; --tocIndex) {
      const int level = levels[static_cast<size_t>(tocIndex)];
      if (level < childLevel) {
        collapsed[static_cast<size_t>(tocIndex)] = 0;
        childLevel = level;
      }
    }
  }
  rows();

  const auto currentRow = std::find(visibleItems.begin(), visibleItems.end(), currentTocIndex);
  scroll = currentRow == visibleItems.end() ? 0 : std::max(0, static_cast<int>(currentRow - visibleItems.begin()) - visibleRows() / 2);
  scroll = std::min(scroll, maxScroll());
  visible = true;

  renderer.syncWriteBufferFromActive();
  render();
}

void TocSidebar::hide() {
  if (!visible) return;
  visible = false;
  if (onDismiss) onDismiss();
}

void TocSidebar::scrollBy(const int rows) {
  const int next = std::max(0, std::min(maxScroll(), scroll + rows));
  if (next == scroll) return;
  scroll = next;
  render();
}

void TocSidebar::select(const int tocIndex) {
  if (!epub || tocIndex < 0 || tocIndex >= epub->getTocItemsCount()) return;
  if (hasChildren(tocIndex)) {
    collapsed[tocIndex] = !collapsed[tocIndex];
    rows();
    render();
    return;
  }

  const int spineIndex = epub->getSpineIndexForTocIndex(tocIndex);
  if (spineIndex < 0) return;
  hide();
  if (onSelect) onSelect(spineIndex);
}

void TocSidebar::render() {
  if (!visible) return;

  renderer.syncWriteBufferFromActive();
  const int panelWidth = width();
  const int screenHeight = renderer.getScreenHeight();
  renderer.rectangle.fill(0, 0, panelWidth, screenHeight, false);
  renderer.rectangle.render(panelWidth - dividerWidth, 0, dividerWidth, screenHeight, true);

  const int titleY = (headerHeight() - renderer.text.getLineHeight(MONTSERRAT_12_FONT_ID)) / 2 + 5;
  renderer.text.render(MONTSERRAT_12_FONT_ID, padding, titleY, "Table of Contents", true);
  const int closeX = panelWidth - padding - closeHitSize;
  const int closeY = (headerHeight() - closeHitSize) / 2;
  renderer.bitmap.iconScaled(Close, closeX + (closeHitSize - closeSize) / 2, closeY + (closeHitSize - closeSize) / 2 + 5,
                             40, 40, closeSize, closeSize, BitmapRender::Orientation::None);
  renderer.line.render(0, headerHeight() - 1, panelWidth, headerHeight() - 1, true);

  const int rows = visibleRows();
  if (visibleItems.empty()) {
    renderer.text.render(MONTSERRAT_10_FONT_ID, padding, headerHeight() + 24, "No table of contents", true);
  } else {
    for (int row = 0; row < rows; ++row) {
      const int visibleIndex = scroll + row;
      if (visibleIndex >= static_cast<int>(visibleItems.size())) break;
      const int tocIndex = visibleItems[visibleIndex];

      const auto item = epub->getTocItem(tocIndex);
      const int y = headerHeight() + row * rowHeight();
      const bool isCurrent = tocIndex == currentTocIndex;
      if (isCurrent) {
        renderer.rectangle.fill(0, y, panelWidth, rowHeight(), static_cast<int>(GfxRenderer::FillTone::Ink));
      }
      const int indent = std::min(48, std::max(0, static_cast<int>(item.level) - 1) * 12);
      const bool parent = hasChildren(tocIndex);
      const int toggleX = padding + indent;
      const int textX = toggleX + (parent ? 22 : 0);
      const int textWidth = std::max(40, panelWidth - textX - padding);
      const std::string title = renderer.text.truncate(MONTSERRAT_10_FONT_ID, item.title.c_str(), textWidth);
      const int textY = y + (rowHeight() - renderer.text.getLineHeight(MONTSERRAT_10_FONT_ID)) / 2;
      if (parent) {
        const char* toggle = collapsed[tocIndex] ? "+" : "−";
        const int toggleY = y + (rowHeight() - renderer.text.getLineHeight(MONTSERRAT_12_FONT_ID)) / 2;
        renderer.text.render(MONTSERRAT_12_FONT_ID, toggleX, toggleY, toggle, !isCurrent);
      }
      renderer.text.render(MONTSERRAT_10_FONT_ID, textX, textY, title.c_str(), !isCurrent);
      if (visibleIndex + 1 < static_cast<int>(visibleItems.size())) {
        renderer.line.render(0, y + rowHeight() - 1, panelWidth, y + rowHeight() - 1, true,
                             LineRender::Style::Dotted);
      }
    }
  }

  const ButtonBounds syncButton = syncButtonBounds();
  constexpr int font = MONTSERRAT_10_FONT_ID;
  const int syncIconY = syncButton.y + (syncButton.height - syncIconSize) / 2;
  Button::render(renderer, syncButton, "", false, font);
  const int textY = syncButton.y + (syncButton.height - renderer.text.getLineHeight(font)) / 2;
  renderer.text.render(font, syncButton.x + padding, textY, "KOReader", true, EpdFontFamily::REGULAR);
  renderer.bitmap.icon(Koreader, syncButton.x + syncButton.width - padding - syncIconSize, syncIconY, syncIconSize,
                       syncIconSize);
  renderer.line.render(0, screenHeight - footerHeight(), panelWidth, screenHeight - footerHeight(), true,
                       LineRender::Style::Dotted);

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void TocSidebar::handleInput(MappedInputManager& input) {
  if (!visible) return;

  // Handle the rotated logical swipe before the panel's native edge-back
  // synthesis: in landscape a vertical TOC scroll can be a physical edge
  // swipe and must not be mistaken for Back.
  if (input.wasTouchSwipeLeftForRenderer(renderer)) {
    hide();
    return;
  }
  if (input.wasTouchSwipeUpForRenderer(renderer)) {
    scrollBy(visibleRows());
    return;
  }
  if (input.wasTouchSwipeDownForRenderer(renderer)) {
    scrollBy(-visibleRows());
    return;
  }
  if (input.wasReleased(MappedInputManager::Button::Back)) {
    hide();
    return;
  }
  if (input.wasPressed(MappedInputManager::Button::Up)) {
    scrollBy(-1);
    return;
  }
  if (input.wasPressed(MappedInputManager::Button::Down)) {
    scrollBy(1);
    return;
  }

  float tapX = 0.0f;
  float tapY = 0.0f;
  if (!input.wasTouchTapInScreen(renderer, tapX, tapY)) return;

  const int x = static_cast<int>(tapX * renderer.getScreenWidth());
  const int y = static_cast<int>(tapY * renderer.getScreenHeight());
  const int panelWidth = width();
  if (x < 0 || x >= panelWidth) {
    hide();
    return;
  }

  const int closeX = panelWidth - padding - closeHitSize;
  const int closeY = (headerHeight() - closeHitSize) / 2;
  if (x >= closeX && x < closeX + closeHitSize && y >= closeY && y < closeY + closeHitSize) {
    hide();
    return;
  }

  const ButtonBounds syncButton = syncButtonBounds();
  const int syncFooterY = renderer.getScreenHeight() - footerHeight();
  if (y >= syncFooterY && y < renderer.getScreenHeight() &&
      x >= syncButton.x && x < syncButton.x + syncButton.width) {
    hide();
    if (onSync) onSync();
    return;
  }

  if (y < headerHeight()) return;
  const int row = (y - headerHeight()) / rowHeight();
  if (row < 0 || row >= visibleRows()) return;
  const int visibleIndex = scroll + row;
  if (visibleIndex >= static_cast<int>(visibleItems.size())) return;
  select(visibleItems[visibleIndex]);
}
