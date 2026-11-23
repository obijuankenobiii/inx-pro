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
#include "system/ScreenComponents.h"

namespace {
constexpr int padding = 20;
constexpr int closeSize = 24;
constexpr int closeHitSize = 40;
constexpr int dividerWidth = 1;
constexpr int footerPadding = 0;
constexpr int syncIconSize = 40;

void drawScrollBar(const GfxRenderer& renderer, const int x, const int y, const int height, const int total,
                   const int visible, const int offset) {
  if (total <= visible || height <= 0) return;

  constexpr int width = 3;
  const int maxOffset = std::max(1, total - visible);
  const int thumbHeight = std::max(14, height * visible / total);
  const int thumbTravel = std::max(1, height - thumbHeight);
  const int thumbY = y + offset * thumbTravel / maxOffset;
  renderer.rectangle.fill(x, y, width, height, static_cast<int>(GfxRenderer::FillTone::Gray), true);
  renderer.rectangle.fill(x, thumbY, width, thumbHeight, static_cast<int>(GfxRenderer::FillTone::Ink), true);
}
}

TocSidebar::TocSidebar(GfxRenderer& renderer, Select onSelect, Dismiss onDismiss, Sync onSync)
    : renderer(renderer), onSelect(std::move(onSelect)), onDismiss(std::move(onDismiss)), onSync(std::move(onSync)) {}

int TocSidebar::width() const {
  const int screenWidth = renderer.getScreenWidth();
  return std::min(screenWidth - 40, std::max(240, screenWidth * 2 / 3));
}

int TocSidebar::headerHeight() const { return rowHeight() + 8; }

int TocSidebar::footerHeight() const { return Button::height + footerPadding * 2; }

int TocSidebar::rowHeight() const {


  return std::max(66, renderer.text.getLineHeight(systemFontId()) + 40);
}

int TocSidebar::visibleRows() const {
  return std::max(1, (renderer.getScreenHeight() - headerHeight() - footerHeight()) / rowHeight());
}

ButtonBounds TocSidebar::syncButtonBounds() const {
  const int footerY = renderer.getScreenHeight() - footerHeight();
  return {0, footerY + footerPadding, width(), Button::height};
}

int TocSidebar::maxScroll() const {
  const int total = rowScanIndex >= tocCount ? static_cast<int>(visibleItems.size()) : tocCount;
  return std::max(0, total - visibleRows());
}

uint8_t TocSidebar::levelAt(const int tocIndex) {
  static constexpr uint8_t kUnknownLevel = 0xFF;
  if (!epub || tocIndex < 0 || tocIndex >= tocCount) return 0;
  if (levels[static_cast<size_t>(tocIndex)] == kUnknownLevel) {
    levels[static_cast<size_t>(tocIndex)] = epub->getTocItem(tocIndex).level;
  }
  return levels[static_cast<size_t>(tocIndex)];
}

bool TocSidebar::hasChildren(const int tocIndex) {
  return tocIndex >= 0 && tocIndex + 1 < tocCount && levelAt(tocIndex + 1) > levelAt(tocIndex);
}

void TocSidebar::ensureRows(const int count) {
  const int requested = std::max(0, count);
  while (static_cast<int>(visibleItems.size()) < requested && rowScanIndex < tocCount) {
    const int tocIndex = rowScanIndex++;
    const int level = levelAt(tocIndex);
    if (hiddenLevel >= 0 && level > hiddenLevel) continue;

    hiddenLevel = -1;
    visibleItems.push_back(tocIndex);
    if (collapsed[static_cast<size_t>(tocIndex)] && hasChildren(tocIndex)) {
      hiddenLevel = level;
    }
  }
}

void TocSidebar::rows() {
  visibleItems.clear();
  rowScanIndex = 0;
  hiddenLevel = -1;
  ensureRows(scroll + visibleRows());
  if (rowScanIndex >= tocCount) {
    scroll = std::min(scroll, std::max(0, static_cast<int>(visibleItems.size()) - visibleRows()));
  }
}

void TocSidebar::show(Epub* book, const int spineIndex) {
  epub = book;
  levels.clear();
  collapsed.clear();
  visibleItems.clear();

  tocCount = epub ? epub->getTocItemsCount() : 0;
  static constexpr uint8_t kUnknownLevel = 0xFF;
  levels.assign(static_cast<size_t>(tocCount), kUnknownLevel);


  collapsed.assign(static_cast<size_t>(tocCount), 1);

  currentTocIndex = epub ? epub->getTocIndexForSpineIndex(spineIndex) : -1;
  scroll = 0;
  rows();




  if (currentTocIndex >= 0 && currentTocIndex < tocCount) {
    int childLevel = levelAt(currentTocIndex);
    for (int index = currentTocIndex - 1; index >= 0; --index) {
      const int parentLevel = levelAt(index);
      if (parentLevel < childLevel) {
        collapsed[static_cast<size_t>(index)] = 0;
        childLevel = parentLevel;
        if (childLevel == 0) break;
      }
    }

    rows();
    ensureRows(currentTocIndex + 1);
    const auto currentVisible =
        std::find(visibleItems.begin(), visibleItems.end(), currentTocIndex);
    if (currentVisible != visibleItems.end()) {
      const int currentRow = static_cast<int>(currentVisible - visibleItems.begin());
      scroll = std::max(0, currentRow - visibleRows() / 2);
      rows();
    }
  }
  visible = true;

  renderer.syncWriteBufferFromActive();
  renderer.freeBwBufferChunks();
  frameStored = renderer.storeBwBuffer();
  render();
}

void TocSidebar::hide() {
  if (!visible) return;
  visible = false;
  lastDismissalRestored = false;
  if (frameStored) {
    if (renderer.copyStoredBwToFramebuffer()) {
      renderer.displayBuffer(HalDisplay::FAST_REFRESH);
      lastDismissalRestored = true;
    }
    releaseStoredFrame();
  }
  if (onDismiss) onDismiss();
}

void TocSidebar::releaseStoredFrame() {
  if (!frameStored) return;
  renderer.freeBwBufferChunks();
  frameStored = false;
}

void TocSidebar::scrollBy(const int rows) {
  const int requested = std::max(0, std::min(maxScroll(), scroll + rows));
  ensureRows(requested + visibleRows());
  const int next = rowScanIndex >= tocCount
                       ? std::max(0, std::min(requested, static_cast<int>(visibleItems.size()) - visibleRows()))
                       : requested;
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

  ensureRows(scroll + visibleRows());
  if (rowScanIndex >= tocCount) {
    scroll = std::min(scroll, std::max(0, static_cast<int>(visibleItems.size()) - visibleRows()));
  }

  if (!frameStored || !renderer.copyStoredBwToFramebuffer()) renderer.syncWriteBufferFromActive();
  ScreenComponents::drawWhiteOverlay(renderer);
  const int panelWidth = width();
  const int screenHeight = renderer.getScreenHeight();
  const int font = systemFontId();
  renderer.rectangle.fill(0, 0, panelWidth, screenHeight, false);
  renderer.rectangle.render(panelWidth - dividerWidth, 0, dividerWidth, screenHeight, true);

  const int titleY = (headerHeight() - renderer.text.getLineHeight(font)) / 2 + 5;
  renderer.text.render(font, padding, titleY, "Table of Contents", true);
  const int closeX = panelWidth - padding - closeHitSize;
  const int closeY = (headerHeight() - closeHitSize) / 2;
  renderer.bitmap.iconScaled(Close, closeX + (closeHitSize - closeSize) / 2, closeY + (closeHitSize - closeSize) / 2 + 5,
                             40, 40, closeSize, closeSize, BitmapRender::Orientation::None);
  renderer.line.render(0, headerHeight() - 1, panelWidth, headerHeight() - 1, true);

  const int rows = visibleRows();
  if (visibleItems.empty()) {
    renderer.text.render(font, padding, headerHeight() + 24, "No table of contents", true);
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
      const std::string title = renderer.text.truncate(font, item.title.c_str(), textWidth);
      const int textY = y + (rowHeight() - renderer.text.getLineHeight(font)) / 2;
      if (parent) {
        const char* toggle = collapsed[tocIndex] ? "+" : "−";
        const int toggleY = y + (rowHeight() - renderer.text.getLineHeight(font)) / 2;
        renderer.text.render(font, toggleX, toggleY, toggle, !isCurrent);
      }
      renderer.text.render(font, textX, textY, title.c_str(), !isCurrent);
      if (visibleIndex + 1 < static_cast<int>(visibleItems.size())) {
        renderer.line.render(0, y + rowHeight() - 1, panelWidth, y + rowHeight() - 1, true,
                             LineRender::Style::Dotted);
      }
    }
  }

  const int scrollTotal = rowScanIndex >= tocCount ? static_cast<int>(visibleItems.size()) : tocCount;
  drawScrollBar(renderer, panelWidth - 8, headerHeight(), rows * rowHeight(), scrollTotal, rows, scroll);

  const ButtonBounds syncButton = syncButtonBounds();
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

  if (input.wasTouchSwipeLeftForRenderer(renderer)) {
    hide();
    return;
  }
  const bool swipeUp = input.wasTouchSwipeUpForRenderer(renderer);
  const bool swipeDown = input.wasTouchSwipeDownForRenderer(renderer);
  if (swipeUp) {
    scrollBy(visibleRows());
    return;
  }
  if (swipeDown) {
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
