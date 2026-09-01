#include "SearchNumpad.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <cctype>
#include <cstring>

#include "images/Delete.h"
#include "images/Shift.h"
#include "system/Fonts.h"

namespace SearchKeyboardLayout::Numpad {
namespace {

constexpr int rows = 4;
constexpr int columns = 4;
constexpr unsigned long multiTapDelay = 1500;
constexpr const char* keys[4][4] = {
    {"abc", "def", "ghi", "jkl"}, {"mno", "pqrs", "tuv", "wxyz"},
    {".,!?", "'\"()", "-/:;", "@#$"}, {"MODE", "SPACE", "", "DEL"}};

const char* textFor(const int mode, const int row, const int col) {
  if (row == 3) return keys[row][col];
  static constexpr const char* numbers[3][4] = {{"1", "2", "3", "4"}, {"5", "6", "7", "8"},
                                                {"9", "0", ".", ","}};
  static constexpr const char* symbols[3][4] = {{"!@#", "$%&", "*+-", "^=~"}, {"/=:", "()[]", "{}<>", "_|\\"},
                                                 {"'`", ";:,.", "?", "&"}};
  if (mode == 2) return numbers[row][col];
  if (mode == 3) return symbols[row][col];
  return keys[row][col];
}

void drawKey(const GfxRenderer& renderer, const int x, const int y, const int width, const int height,
             const char* label, const bool selected) {
  if (selected) {
    renderer.rectangle.fill(x, y, width, height, true, false);
  } else {
    renderer.rectangle.fill(x, y, width, height, false, false);
  }
  renderer.line.render(x, y, x + width - 1, y, true);
  renderer.line.render(x + width - 1, y, x + width - 1, y + height - 1, true);

  const int font = systemFontId();
  const int textWidth = renderer.text.getWidth(font, label);
  const int textY = y + (height - renderer.text.getLineHeight(font)) / 2;
  renderer.text.render(font, x + (width - textWidth) / 2, textY, label, !selected);
}

}

int height(const int top, const int bottom) { return std::max(0, bottom - top); }

void render(const GfxRenderer& renderer, const int top, const int bottom, const State& state) {
  const int keyHeight = std::max(1, (bottom - top) / rows);
  const int width = renderer.getScreenWidth();
  const int keyboardWidth = keyHeight * columns;
  const int startX = (width - keyboardWidth) / 2;

  for (int row = 0; row < rows; ++row) {
    const int y = top + row * keyHeight;
    for (int col = 0; col < columns; ++col) {
      const int x = startX + keyHeight * col;
      const int right = startX + keyHeight * (col + 1);
      const char* label = textFor(state.mode, row, col);
      if (row == 3 && col == 2) {
        drawKey(renderer, x, y, right - x, keyHeight, "", false);
        constexpr int iconSize = 30;
        renderer.bitmap.icon(Shift, x + (right - x - iconSize) / 2, y + (keyHeight - iconSize) / 2, iconSize,
                             iconSize, BitmapRender::Orientation::Rotate180);
      } else if (row == 3 && col == 3) {
        drawKey(renderer, x, y, right - x, keyHeight, "", false);
        constexpr int iconSize = 38;
        renderer.bitmap.iconScaled(Delete, x + (right - x - iconSize) / 2, y + (keyHeight - iconSize) / 2,
                                   30, 30, iconSize, iconSize, BitmapRender::Orientation::None, false);
      } else {
        drawKey(renderer, x, y, right - x, keyHeight, label, false);
      }
    }
  }

  renderer.line.render(startX, top, startX, bottom - 1, true);
  renderer.line.render(startX, bottom - 1, startX + keyboardWidth - 1, bottom - 1, true);
}

bool tap(const GfxRenderer& renderer, const int top, const int bottom, const int x, const int y, State& state,
         std::string& value) {
  if (y < top || y >= bottom || x < 0 || x >= renderer.getScreenWidth()) return false;

  const int keyHeight = std::max(1, (bottom - top) / rows);
  const int row = std::min(rows - 1, (y - top) / keyHeight);
  const int keyboardWidth = keyHeight * columns;
  const int startX = (renderer.getScreenWidth() - keyboardWidth) / 2;
  if (x < startX || x >= startX + keyboardWidth) return true;
  const int col = std::min(columns - 1, (x - startX) / keyHeight);

  if (row == 3) {
    if (col == 0) {
      state.mode = (state.mode + 1) % 4;
      state.row = -1;
      state.col = -1;
    } else if (col == 1) {
      value.push_back(' ');
      state.row = -1;
      state.col = -1;
    } else if (col == 2) {
      state.collapse = true;
      state.row = -1;
      state.col = -1;
    } else if (!value.empty()) {
      value.pop_back();
      state.row = -1;
      state.col = -1;
    }
    return true;
  }

  const char* group = textFor(state.mode, row, col);
  const size_t length = strlen(group);
  if (length == 0) return true;

  const unsigned long now = millis();
  const bool repeat = state.row == row && state.col == col && now < state.until && !value.empty();
  const size_t next = repeat ? (state.index + 1) % length : 0;
  char c = group[next];
  if (state.mode == 0) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (state.mode == 1) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  if (repeat) {
    value.back() = c;
  } else {
    value.push_back(c);
  }
  state.row = row;
  state.col = col;
  state.index = next;
  state.until = now + multiTapDelay;
  return true;
}

bool consumeCollapse(State& state) {
  const bool collapse = state.collapse;
  state.collapse = false;
  return collapse;
}

}
