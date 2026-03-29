#include "SearchKeyboard.h"

#include <algorithm>

#include <GfxRenderer.h>

#include "state/SystemSetting.h"

int SearchKeyboard::height(const GfxRenderer& renderer) const {
  if (SETTINGS.keyboardLayout == SystemSetting::KEYBOARD_NUMPAD) {
    const int keySize = std::max(1, std::min({renderer.getScreenWidth() / 3, (renderer.getScreenHeight() - 145) / 4, 107}));
    return keySize * 4;
  }
  return SearchKeyboardLayout::Qwerty::height(renderer);
}

void SearchKeyboard::render(const GfxRenderer& renderer, const int top, const int bottom) const {
  if (SETTINGS.keyboardLayout == SystemSetting::KEYBOARD_NUMPAD) {
    SearchKeyboardLayout::Numpad::render(renderer, top, bottom, numpad);
  } else {
    SearchKeyboardLayout::Qwerty::render(renderer, top, bottom, qwerty);
  }
}

bool SearchKeyboard::tap(const GfxRenderer& renderer, const int top, const int bottom, const int x, const int y,
                         std::string& value) {
  if (SETTINGS.keyboardLayout == SystemSetting::KEYBOARD_NUMPAD) {
    return SearchKeyboardLayout::Numpad::tap(renderer, top, bottom, x, y, numpad, value);
  }
  return SearchKeyboardLayout::Qwerty::tap(renderer, top, bottom, x, y, qwerty, value);
}

bool SearchKeyboard::consumeGo() { return SearchKeyboardLayout::Qwerty::consumeGo(qwerty); }

bool SearchKeyboard::consumeCollapse() {
  if (SETTINGS.keyboardLayout == SystemSetting::KEYBOARD_NUMPAD) {
    return SearchKeyboardLayout::Numpad::consumeCollapse(numpad);
  }
  return SearchKeyboardLayout::Qwerty::consumeCollapse(qwerty);
}
