#include "BaseTemperatureActivity.h"

#include <GfxRenderer.h>

#include <string>
#include <vector>

#include "activity/page/SubPage.h"
#include "activity/page/components/global/PopUp.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"

namespace {
bool inside(const int x, const int y, const int left, const int top, const int width, const int height) {
  return x >= left && x < left + width && y >= top && y < top + height;
}
}

void BaseTemperatureActivity::onEnter() { render(); }

void BaseTemperatureActivity::render() {
  renderer.clearScreen();
  const int contentTop = SubPage::header(renderer, "Temperature");
  const int font = systemFontId();
  const int width = renderer.getScreenWidth();
  const int textY = contentTop + (kRowHeight - renderer.text.getLineHeight(font)) / 2;

  renderer.rectangle.fill(0, contentTop, width, kRowHeight, static_cast<int>(GfxRenderer::FillTone::Paper));
  renderer.text.render(font, 20, textY, "Unit", true, EpdFontFamily::REGULAR);
  const char* unit = fahrenheit_ ? "Fahrenheit" : "Celsius";
  const int unitWidth = renderer.text.getWidth(font, unit);
  renderer.text.render(font, width - unitWidth - 20, textY, unit, true, EpdFontFamily::REGULAR);
  renderer.line.render(0, contentTop + kRowHeight - 1, width, contentTop + kRowHeight - 1, true,
                       LineRender::Style::Dotted);

  if (unitPopup_) renderUnitPopup();
  renderer.displayBuffer();
}

void BaseTemperatureActivity::renderUnitPopup() {
  const std::vector<std::string> values = {"Celsius", "Fahrenheit"};
  const PopUpBounds box = PopUp::bounds(renderer, static_cast<int>(values.size()));
  PopUp::background(renderer, box);
  PopUp::title(renderer, box, "Unit");
  PopUp::list(renderer, box, values, fahrenheit_ ? 1 : 0, 0);
  PopUp::border(renderer, box);
}

void BaseTemperatureActivity::close() {
  if (onApply_) onApply_(fahrenheit_);
  if (onBack_) onBack_();
}

void BaseTemperatureActivity::handleTouch(const int x, const int y) {
  if (unitPopup_) {
    const PopUpBounds box = PopUp::bounds(renderer, 2);
    if (!inside(x, y, box.x, box.y, box.width, box.height)) {
      unitPopup_ = false;
      render();
      return;
    }
    const int optionY = y - box.y - box.header;
    if (optionY < 0 || optionY >= box.rows * box.row) return;
    const int selected = optionY / box.row;
    if (selected == 0 || selected == 1) {
      fahrenheit_ = selected == 1;
      unitPopup_ = false;
      render();
    }
    return;
  }

  const int contentTop = FREEINK_DEVICE_X4PRO ? 80 : 70;
  if (inside(x, y, 0, contentTop, renderer.getScreenWidth(), kRowHeight)) {
    unitPopup_ = true;
    render();
  }
}

void BaseTemperatureActivity::loop() {
  if (unitPopup_) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      unitPopup_ = false;
      render();
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
    if (mappedInput.wasPressed(MappedInputManager::Button::Up) ||
        mappedInput.wasPressed(MappedInputManager::Button::Down)) {
      fahrenheit_ = !fahrenheit_;
      unitPopup_ = false;
      render();
    }
    return;
  }

  if (SubPage::closeInput(renderer, mappedInput, [this] { close(); })) return;
  if (mappedInput.hasTouch()) {
    float nx = 0.0f;
    float ny = 0.0f;
    if (mappedInput.wasTouchTapInScreen(renderer, nx, ny)) {
      handleTouch(static_cast<int>(nx * renderer.getScreenWidth()),
                  static_cast<int>(ny * renderer.getScreenHeight()));
      return;
    }
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    close();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    unitPopup_ = true;
    render();
  }
}
