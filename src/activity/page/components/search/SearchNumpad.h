#pragma once

#include <string>

class GfxRenderer;

namespace SearchKeyboardLayout {
namespace Numpad {

struct State {
  int mode = 0;
  int row = -1;
  int col = -1;
  size_t index = 0;
  unsigned long until = 0;
  bool collapse = false;
};

int height(int top, int bottom);
void render(const GfxRenderer& renderer, int top, int bottom, const State& state);
bool tap(const GfxRenderer& renderer, int top, int bottom, int x, int y, State& state, std::string& value);
bool consumeCollapse(State& state);

}  // namespace Numpad
}  // namespace SearchKeyboardLayout
