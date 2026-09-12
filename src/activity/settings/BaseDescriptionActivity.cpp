#include "BaseDescriptionActivity.h"

#include <GfxRenderer.h>

#include "activity/page/SubPage.h"
#include "activity/page/components/global/Toggle.h"
#include "system/Fonts.h"

namespace {
bool inside(const int x, const int y, const int left, const int top, const int width, const int height) {
  return x >= left && x < left + width && y >= top && y < top + height;
}
}

void BaseDescriptionActivity::onEnter() { render(); }

void BaseDescriptionActivity::render() {
  renderer.clearScreen();
  const int contentTop = SubPage::header(renderer, "Description");
  const int font = systemFontId();
  const int width = renderer.getScreenWidth();
  const char* labels[] = {"Background", "Show title", "Show author", "Show progress"};
  const bool values[] = {background_, showTitle_, showAuthor_, showProgress_};

  for (int row = 0; row < 4; ++row) {
    const int rowY = contentTop + row * kRowHeight;
    renderer.rectangle.fill(0, rowY, width, kRowHeight, static_cast<int>(GfxRenderer::FillTone::Paper));
    renderer.text.render(font, 20, rowY + (kRowHeight - renderer.text.getLineHeight(font)) / 2, labels[row], true,
                         EpdFontFamily::REGULAR);
    Toggle::render(renderer, width - 20, rowY, kRowHeight, values[row]);
    if (row > 0) renderer.line.render(0, rowY - 1, width, rowY - 1, true, LineRender::Style::Dotted);
  }
  renderer.displayBuffer();
}

void BaseDescriptionActivity::close() {
  if (onApply_) onApply_(background_, showTitle_, showAuthor_, showProgress_);
  if (onBack_) onBack_();
}

void BaseDescriptionActivity::handleTouch(const int x, const int y) {
  const int contentTop = FREEINK_DEVICE_X4PRO ? 80 : 70;
  if (!inside(x, y, 0, contentTop, renderer.getScreenWidth(), kRowHeight * 4)) return;
  const int row = (y - contentTop) / kRowHeight;
  switch (row) {
    case 0:
      background_ = !background_;
      break;
    case 1:
      showTitle_ = !showTitle_;
      break;
    case 2:
      showAuthor_ = !showAuthor_;
      break;
    case 3:
      showProgress_ = !showProgress_;
      break;
    default:
      return;
  }
  render();
}

void BaseDescriptionActivity::loop() {
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
      return;
    }
  }
}
