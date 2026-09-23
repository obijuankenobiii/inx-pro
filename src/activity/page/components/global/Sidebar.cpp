#include "Sidebar.h"

#include <GfxRenderer.h>

#include <algorithm>

#include "../../navigation/Menu.h"
#include "system/Fonts.h"

namespace {
constexpr int kInnerPadding = 16;
constexpr int kTopPadding = 24;
constexpr int kRowGap = 8;
}

int Sidebar::width(const GfxRenderer& renderer) {
  return std::min(320, renderer.getScreenWidth() * 2 / 4);
}

int Sidebar::listTop() { return navigation::Menu::height + 34; }

int Sidebar::visibleRows(const GfxRenderer& renderer) {
  const int contentTop = listTop() + kTopPadding;
  const int contentBottom = renderer.getScreenHeight() - kInnerPadding;
  return std::max(1, (contentBottom - contentTop + kRowGap) / (rowHeight + kRowGap));
}

void Sidebar::renderFrame(const GfxRenderer& renderer, const char* title) {
  const int drawerWidth = width(renderer);
  renderer.rectangle.fill(0, 0, drawerWidth, renderer.getScreenHeight(), false);
  renderer.line.render(drawerWidth - 1, 0, drawerWidth - 1, renderer.getScreenHeight(), true);
  renderer.text.render(MONTSERRAT_16_FONT_ID, 20, 25, title ? title : "", true, EpdFontFamily::BOLD);
  renderer.line.render(20, navigation::Menu::height + 14, drawerWidth - 20,
                       navigation::Menu::height + 14, true);
}

void Sidebar::renderTextList(const GfxRenderer& renderer, const char* const* labels, const size_t count,
                             int scrollOffset) {
  const int font = systemFontId();
  const int lineHeight = renderer.text.getLineHeight(font);
  const int x = kInnerPadding + 16;
  const int rows = visibleRows(renderer);
  scrollOffset = std::max(0, std::min(scrollOffset, std::max(0, static_cast<int>(count) - rows)));
  for (int row = 0; row < rows && scrollOffset + row < static_cast<int>(count); ++row) {
    const int y = listTop() + kTopPadding + row * (rowHeight + kRowGap);
    const char* label = labels[scrollOffset + row];
    renderer.text.render(font, x, y + (rowHeight - lineHeight) / 2, label ? label : "", true);
  }
  if (static_cast<int>(count) > rows) {
    const int top = listTop() + kTopPadding;
    const int height = rows * (rowHeight + kRowGap) - kRowGap;
    const int thumbHeight = std::max(12, height * rows / static_cast<int>(count));
    const int maxScroll = static_cast<int>(count) - rows;
    const int thumbY = top + (height - thumbHeight) * scrollOffset / maxScroll;
    renderer.rectangle.fill(width(renderer) - 4, thumbY, 2, thumbHeight, true);
  }
}

int Sidebar::hitTest(const GfxRenderer& renderer, const int tapX, const int tapY, const size_t count,
                     const int scrollOffset) {
  const int drawerWidth = width(renderer);
  const int contentTop = listTop() + kTopPadding;
  const int contentBottom = renderer.getScreenHeight() - kInnerPadding;
  if (tapX < kInnerPadding || tapX >= drawerWidth - kInnerPadding || tapY < contentTop ||
      tapY >= contentBottom) {
    return -1;
  }
  const int index = (tapY - contentTop) / (rowHeight + kRowGap);
  if (index < 0 || index >= visibleRows(renderer) ||
      static_cast<size_t>(scrollOffset + index) >= count) return -1;
  const int rowY = contentTop + index * (rowHeight + kRowGap);
  return tapY < rowY + rowHeight ? scrollOffset + index : -1;
}
