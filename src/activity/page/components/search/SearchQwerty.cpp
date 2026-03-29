#include "SearchQwerty.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <cctype>
#include <cstring>

#include "images/Delete.h"
#include "images/Enter.h"
#include "images/Shift.h"
#include "images/Space.h"
#include "system/Fonts.h"

namespace SearchKeyboardLayout::Qwerty {
namespace {

constexpr int margin = 6;
constexpr int gap = 6;
constexpr int rows = 4;
constexpr int columns = 10;

constexpr const char* lettersTop = "qwertyuiop";
constexpr const char* lettersMiddle = "asdfghjkl";
constexpr const char* lettersBottom = "zxcvbnm";
constexpr const char* symbolsTop = "1234567890";
constexpr const char* symbolsMiddle = "-/:;()$&@\"";
constexpr const char* symbolsBottom = ".,?!'";
constexpr const char* extraTop = "[]{}#%^*+=";
constexpr const char* extraMiddle = "_\\|~<>[]{}";
constexpr const char* extraBottom = "`^=+_";

struct Geometry {
  int left = 0;
  int top = 0;
  int width = 0;
  int keyHeight = 0;
  int keyWidth = 0;
};

Geometry geometry(const GfxRenderer& renderer, const int top, const int bottom) {
  Geometry value;
  value.left = margin;
  value.width = renderer.getScreenWidth() - margin * 2;
  value.keyWidth = std::max(1, (value.width - gap * (columns - 1)) / columns);
  value.keyHeight = value.keyWidth + 12;
  const int keyboardHeight = value.keyHeight * rows + gap * (rows - 1);
  value.top = std::max(top, bottom - keyboardHeight);
  return value;
}

int rowY(const Geometry& value, const int row) { return value.top + row * (value.keyHeight + gap); }

void drawKey(const GfxRenderer& renderer, const int x, const int y, const int width, const int height,
             const char* label, const bool filled = false) {
  if (filled) {
    renderer.rectangle.fill(x, y, width, height, true, false);
  }
  renderer.rectangle.render(x, y, width, height, true);

  if (!label || label[0] == '\0') return;
  constexpr int font = MONTSERRAT_16_FONT_ID;
  const int textWidth = renderer.text.getWidth(font, label);
  const int textY = y + (height - renderer.text.getLineHeight(font)) / 2;
  renderer.text.render(font, x + (width - textWidth) / 2, textY, label, !filled);
}

void drawDelete(const GfxRenderer& renderer, const int x, const int y, const int width, const int height) {
  renderer.rectangle.render(x, y, width, height, true);
  constexpr int size = 34;
  renderer.bitmap.iconScaled(Delete, x + (width - size) / 2, y + (height - size) / 2, 30, 30, size, size,
                             BitmapRender::Orientation::None, false);
}

void drawShift(const GfxRenderer& renderer, const int x, const int y, const int width, const int height,
               const bool active) {
  drawKey(renderer, x, y, width, height, "", active);
  constexpr int size = 30;
  renderer.bitmap.icon(Shift, x + (width - size) / 2, y + (height - size) / 2, size, size,
                       BitmapRender::Orientation::None, active);
}

void drawEnter(const GfxRenderer& renderer, const int x, const int y, const int width, const int height) {
  drawKey(renderer, x, y, width, height, "", true);
  constexpr int size = 40;
  renderer.bitmap.icon(Enter, x + (width - size) / 2, y + (height - size) / 2, size, size,
                       BitmapRender::Orientation::None, true);
}

void drawSpace(const GfxRenderer& renderer, const int x, const int y, const int width, const int height) {
  drawKey(renderer, x, y, width, height, "");
  constexpr int size = 40;
  renderer.bitmap.icon(Space, x + (width - size) / 2, y + (height - size) / 2, size, size);
}

void drawCollapse(const GfxRenderer& renderer, const int x, const int y, const int width, const int height) {
  drawKey(renderer, x, y, width, height, "");
  constexpr int size = 30;
  renderer.bitmap.icon(Shift, x + (width - size) / 2, y + (height - size) / 2, size, size,
                       BitmapRender::Orientation::Rotate180);
}

int rowWidth(const int keyWidth, const int count) { return keyWidth * count + gap * (count - 1); }

const char* topKeys(const State& state) {
  if (state.mode == Mode::Symbols) return symbolsTop;
  if (state.mode == Mode::ExtraSymbols) return extraTop;
  return lettersTop;
}

const char* middleKeys(const State& state) {
  if (state.mode == Mode::Symbols) return symbolsMiddle;
  if (state.mode == Mode::ExtraSymbols) return extraMiddle;
  return lettersMiddle;
}

const char* bottomKeys(const State& state) {
  if (state.mode == Mode::Symbols) return symbolsBottom;
  if (state.mode == Mode::ExtraSymbols) return extraBottom;
  return lettersBottom;
}

char displayCharacter(const State& state, const char value) {
  if (state.mode == Mode::Letters && (state.caps || state.capsLock)) {
    return static_cast<char>(std::toupper(static_cast<unsigned char>(value)));
  }
  return value;
}

void append(std::string& value, const char character) { value.push_back(character); }

}  // namespace

int height(const GfxRenderer& renderer) {
  const int width = renderer.getScreenWidth() - margin * 2;
  const int keyWidth = std::max(1, (width - gap * (columns - 1)) / columns);
  return (keyWidth + 12) * rows + gap * (rows - 1);
}

void render(const GfxRenderer& renderer, const int top, const int bottom, const State& state) {
  const Geometry value = geometry(renderer, top, bottom);
  const int right = value.left + value.width;

  const auto drawCharacters = [&](const int row, const char* keys) {
    const int count = static_cast<int>(std::strlen(keys));
    const int width = rowWidth(value.keyWidth, count);
    int x = (renderer.getScreenWidth() - width) / 2;
    for (int index = 0; index < count; ++index) {
      char label[2] = {displayCharacter(state, keys[index]), '\0'};
      drawKey(renderer, x, rowY(value, row), value.keyWidth, value.keyHeight, label);
      x += value.keyWidth + gap;
    }
  };

  drawCharacters(0, topKeys(state));
  drawCharacters(1, middleKeys(state));

  const char* lower = bottomKeys(state);
  const int count = static_cast<int>(std::strlen(lower));
  const int actionWidth = std::max(value.keyWidth,
                                   (value.width - count * value.keyWidth - gap * (count + 1)) / 2);
  const int y = rowY(value, 2);
  int x = value.left;
  if (state.mode == Mode::Letters) {
    drawShift(renderer, x, y, actionWidth, value.keyHeight, state.caps || state.capsLock);
  } else {
    drawKey(renderer, x, y, actionWidth, value.keyHeight, "#+=");
  }
  x += actionWidth + gap;
  for (int index = 0; index < count; ++index) {
    char label[2] = {displayCharacter(state, lower[index]), '\0'};
    drawKey(renderer, x, y, value.keyWidth, value.keyHeight, label);
    x += value.keyWidth + gap;
  }
  drawDelete(renderer, x, y, std::max(1, right - x), value.keyHeight);

  const int bottomY = rowY(value, 3);
  if (state.mode == Mode::Letters) {
    const int modeWidth = value.width * 24 / 100;
    const int dotWidth = value.keyWidth + 4;
    const int collapseWidth = value.keyWidth + 4;
    const int goWidth = value.width * 21 / 100;
    const int spaceWidth = value.width - modeWidth - collapseWidth - dotWidth - goWidth - gap * 4;
    x = value.left;
    drawKey(renderer, x, bottomY, modeWidth, value.keyHeight, "123");
    x += modeWidth + gap;
    drawSpace(renderer, x, bottomY, spaceWidth, value.keyHeight);
    x += spaceWidth + gap;
    drawCollapse(renderer, x, bottomY, collapseWidth, value.keyHeight);
    x += collapseWidth + gap;
    drawKey(renderer, x, bottomY, dotWidth, value.keyHeight, ".");
    x += dotWidth + gap;
    drawEnter(renderer, x, bottomY, std::max(1, right - x), value.keyHeight);
  } else {
    const int modeWidth = value.width * 26 / 100;
    const int collapseWidth = value.keyWidth + 4;
    const int goWidth = value.width * 28 / 100;
    const int spaceWidth = value.width - modeWidth - collapseWidth - goWidth - gap * 3;
    x = value.left;
    drawKey(renderer, x, bottomY, modeWidth, value.keyHeight, "ABC");
    x += modeWidth + gap;
    drawSpace(renderer, x, bottomY, spaceWidth, value.keyHeight);
    x += spaceWidth + gap;
    drawCollapse(renderer, x, bottomY, collapseWidth, value.keyHeight);
    x += collapseWidth + gap;
    drawEnter(renderer, x, bottomY, std::max(1, right - x), value.keyHeight);
  }
}

bool tap(const GfxRenderer& renderer, const int top, const int bottom, const int tapX, const int tapY, State& state,
         std::string& value) {
  const Geometry layout = geometry(renderer, top, bottom);
  if (tapX < layout.left || tapX >= layout.left + layout.width || tapY < layout.top || tapY >= bottom) return false;

  const int row = (tapY - layout.top) / (layout.keyHeight + gap);
  const int rowOffset = (tapY - layout.top) % (layout.keyHeight + gap);
  if (row < 0 || row >= rows || rowOffset >= layout.keyHeight) return true;

  const auto tapCharacters = [&](const char* keys) {
    const int count = static_cast<int>(std::strlen(keys));
    const int width = rowWidth(layout.keyWidth, count);
    const int start = (renderer.getScreenWidth() - width) / 2;
    if (tapX < start || tapX >= start + width) return;
    const int offset = tapX - start;
    const int col = offset / (layout.keyWidth + gap);
    if (col < 0 || col >= count || offset % (layout.keyWidth + gap) >= layout.keyWidth) return;
    append(value, displayCharacter(state, keys[col]));
    if (state.mode == Mode::Letters && state.caps && !state.capsLock) state.caps = false;
  };

  if (row == 0) {
    tapCharacters(topKeys(state));
    return true;
  }
  if (row == 1) {
    tapCharacters(middleKeys(state));
    return true;
  }

  if (row == 2) {
    const char* keys = bottomKeys(state);
    const int count = static_cast<int>(std::strlen(keys));
    const int actionWidth = std::max(layout.keyWidth,
                                     (layout.width - count * layout.keyWidth - gap * (count + 1)) / 2);
    const int deleteX = layout.left + actionWidth + gap + count * (layout.keyWidth + gap);
    if (tapX < layout.left + actionWidth) {
      if (state.mode == Mode::Letters) {
        // off -> one-shot shift -> caps lock -> off
        if (state.capsLock) {
          state.capsLock = false;
          state.caps = false;
        } else if (state.caps) {
          state.caps = false;
          state.capsLock = true;
        } else {
          state.caps = true;
        }
      } else {
        state.mode = state.mode == Mode::Symbols ? Mode::ExtraSymbols : Mode::Symbols;
      }
      return true;
    }
    if (tapX >= deleteX) {
      if (!value.empty()) value.pop_back();
      return true;
    }
    const int offset = tapX - (layout.left + actionWidth + gap);
    const int col = offset / (layout.keyWidth + gap);
    if (col >= 0 && col < count && offset % (layout.keyWidth + gap) < layout.keyWidth) {
      append(value, displayCharacter(state, keys[col]));
      if (state.mode == Mode::Letters && state.caps && !state.capsLock) state.caps = false;
    }
    return true;
  }

  if (state.mode == Mode::Letters) {
    const int modeWidth = layout.width * 24 / 100;
    const int dotWidth = layout.keyWidth + 4;
    const int collapseWidth = layout.keyWidth + 4;
    const int goWidth = layout.width * 21 / 100;
    const int spaceWidth = layout.width - modeWidth - collapseWidth - dotWidth - goWidth - gap * 4;
    int x = layout.left;
    if (tapX < x + modeWidth) {
      state.mode = Mode::Symbols;
    } else if ((x += modeWidth + gap), tapX < x + spaceWidth) {
      append(value, ' ');
    } else if ((x += spaceWidth + gap), tapX < x + collapseWidth) {
      state.collapse = true;
    } else if ((x += collapseWidth + gap), tapX < x + dotWidth) {
      append(value, '.');
    } else {
      state.go = true;
    }
  } else {
    const int modeWidth = layout.width * 26 / 100;
    const int collapseWidth = layout.keyWidth + 4;
    const int goWidth = layout.width * 28 / 100;
    const int spaceWidth = layout.width - modeWidth - collapseWidth - goWidth - gap * 3;
    const int spaceX = layout.left + modeWidth + gap;
    const int collapseX = spaceX + spaceWidth + gap;
    if (tapX < layout.left + modeWidth) {
      state.mode = Mode::Letters;
      state.caps = false;
    } else if (tapX >= spaceX && tapX < spaceX + spaceWidth) {
      append(value, ' ');
    } else if (tapX >= collapseX && tapX < collapseX + collapseWidth) {
      state.collapse = true;
    } else {
      state.go = true;
    }
  }
  return true;
}

bool consumeGo(State& state) {
  const bool go = state.go;
  state.go = false;
  return go;
}

bool consumeCollapse(State& state) {
  const bool collapse = state.collapse;
  state.collapse = false;
  return collapse;
}

}  // namespace SearchKeyboardLayout::Qwerty
