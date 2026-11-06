#include "BaseStreaksActivity.h"

#include <GfxRenderer.h>

#include "activity/page/SubPage.h"
#include "activity/page/components/global/Toggle.h"
#include "system/Fonts.h"

namespace {
bool inside(const int x, const int y, const int left, const int top, const int width, const int height) {
  return x >= left && x < left + width && y >= top && y < top + height;
}
}

void BaseStreaksActivity::onEnter() { render(); }

void BaseStreaksActivity::render() {
  renderer.clearScreen();
  contentTop_ = SubPage::header(renderer, "Streaks");
  const int font = systemFontId();
  const int width = renderer.getScreenWidth();
  const char* labels[] = {"Compact"};
  const bool values[] = {compact_};
  for (int row = 0; row < 1; ++row) {
    const int rowY = contentTop_ + row * kRowHeight;
    renderer.rectangle.fill(0, rowY, width, kRowHeight, static_cast<int>(GfxRenderer::FillTone::Paper));
    renderer.text.render(font, 20, rowY + (kRowHeight - renderer.text.getLineHeight(font)) / 2, labels[row], true,
                         EpdFontFamily::REGULAR);
    Toggle::render(renderer, width - 20, rowY, kRowHeight, values[row]);
    if (row > 0) renderer.line.render(0, rowY - 1, width, rowY - 1, true, LineRender::Style::Dotted);
  }
  renderer.displayBuffer();
}

void BaseStreaksActivity::close() {
  if (onApply_) onApply_(compact_);
  if (onBack_) onBack_();
}

void BaseStreaksActivity::handleTouch(const int x, const int y) {
  if (!inside(x, y, 0, contentTop_, renderer.getScreenWidth(), kRowHeight)) return;
  const int row = (y - contentTop_) / kRowHeight;
  if (row == 0) compact_ = !compact_;
  render();
}

void BaseStreaksActivity::loop() {
  if (SubPage::closeInput(renderer, mappedInput, [this] { close(); })) return;
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    close();
    return;
  }
  if (mappedInput.hasTouch()) {
    float nx = 0.0f;
    float ny = 0.0f;
    if (mappedInput.wasTouchTapInScreen(renderer, nx, ny)) {
      handleTouch(static_cast<int>(nx * renderer.getScreenWidth()),
                  static_cast<int>(ny * renderer.getScreenHeight()));
    }
  }
}
