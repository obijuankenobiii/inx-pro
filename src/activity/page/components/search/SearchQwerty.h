#pragma once

#include <string>

class GfxRenderer;

namespace SearchKeyboardLayout {
namespace Qwerty {

enum class Mode { Letters, Symbols, ExtraSymbols };

struct State {
  /** One-shot shift: cleared after the next letter. */
  bool caps = false;
  /** Sticky shift, unaffected by keypresses. Tapping shift cycles off -> shift -> caps lock -> off. */
  bool capsLock = false;
  bool go = false;
  bool collapse = false;
  Mode mode = Mode::Letters;
};

int height(const GfxRenderer& renderer);
void render(const GfxRenderer& renderer, int top, int bottom, const State& state);
bool tap(const GfxRenderer& renderer, int top, int bottom, int x, int y, State& state, std::string& value);
bool consumeGo(State& state);
bool consumeCollapse(State& state);

}  // namespace Qwerty
}  // namespace SearchKeyboardLayout
