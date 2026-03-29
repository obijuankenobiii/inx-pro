#pragma once

#include <string>

#include "SearchNumpad.h"
#include "SearchQwerty.h"

class GfxRenderer;

class SearchKeyboard {
 public:
  SearchKeyboard() = default;

  int height(const GfxRenderer& renderer) const;
  void render(const GfxRenderer& renderer, int top, int bottom) const;
  bool tap(const GfxRenderer& renderer, int top, int bottom, int x, int y, std::string& value);
  bool consumeGo();
  bool consumeCollapse();

 private:
  SearchKeyboardLayout::Qwerty::State qwerty;
  SearchKeyboardLayout::Numpad::State numpad;
};
