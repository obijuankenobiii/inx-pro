#include "BaseStatisticsActivity.h"

#include <GfxRenderer.h>

#include <string>
#include <vector>

#include "activity/page/SubPage.h"
#include "activity/page/components/global/PopUp.h"
#include "activity/page/components/global/Toggle.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"

namespace {
bool inside(const int x, const int y, const int left, const int top, const int width, const int height) {
  return x >= left && x < left + width && y >= top && y < top + height;
}
}

void BaseStatisticsActivity::onEnter() { render(); }

void BaseStatisticsActivity::render() {
  renderer.clearScreen();
  const int contentTop = SubPage::header(renderer, "Statistics");
  const int font = systemFontId();
  const int width = renderer.getScreenWidth();
  const int lineHeight = renderer.text.getLineHeight(font);
  renderer.rectangle.fill(0, contentTop, width, kRowHeight * 2, static_cast<int>(GfxRenderer::FillTone::Paper));
  renderer.text.render(font, 20, contentTop + (kRowHeight - lineHeight) / 2, "Style", true,
                       EpdFontFamily::REGULAR);
  const char* kStyle = lineChart_ ? "Line Chart" : "Bar Chart";
  const int styleWidth = renderer.text.getWidth(font, kStyle);
  renderer.text.render(font, width - styleWidth - 20, contentTop + (kRowHeight - lineHeight) / 2, kStyle, true,
                       EpdFontFamily::REGULAR);
  const int summaryY = contentTop + kRowHeight;
  renderer.text.render(font, 20, summaryY + (kRowHeight - lineHeight) / 2, "Summary", true,
                       EpdFontFamily::REGULAR);
  Toggle::render(renderer, width - 20, summaryY, kRowHeight, showSummary_);
  renderer.line.render(SettingsListLayout::kDividerInset, summaryY - 1,
                       width - SettingsListLayout::kDividerInset, summaryY - 1, true,
                       LineRender::Style::Dotted);
  if (stylePopup_) renderStylePopup();
  renderer.displayBuffer();
}

void BaseStatisticsActivity::renderStylePopup() {
  const std::vector<std::string> values = {"Bar Chart", "Line Chart"};
  const PopUpBounds box = PopUp::bounds(renderer, static_cast<int>(values.size()));
  PopUp::background(renderer, box);
  PopUp::title(renderer, box, "Style");
  PopUp::list(renderer, box, values, lineChart_ ? 1 : 0, 0);
  PopUp::border(renderer, box);
}

void BaseStatisticsActivity::close() {
  if (onApply_) onApply_(lineChart_, showSummary_);
  if (onBack_) onBack_();
}

void BaseStatisticsActivity::handleTouch(const int x, const int y) {
  if (stylePopup_) {
    const PopUpBounds box = PopUp::bounds(renderer, 2);
    if (!inside(x, y, box.x, box.y, box.width, box.height)) {
      stylePopup_ = false;
      render();
    } else {
      const int optionY = y - box.y - box.header;
      if (optionY >= 0 && optionY < box.rows * box.row) {
        lineChart_ = optionY / box.row == 1;
        stylePopup_ = false;
        render();
      }
    }
    return;
  }
  const int contentTop = FREEINK_DEVICE_X4PRO ? 80 : 70;
  if (inside(x, y, 0, contentTop, renderer.getScreenWidth(), kRowHeight)) {
    stylePopup_ = true;
    render();
  } else if (inside(x, y, 0, contentTop + kRowHeight, renderer.getScreenWidth(), kRowHeight)) {
    showSummary_ = !showSummary_;
    render();
  }
}

void BaseStatisticsActivity::loop() {
  if (stylePopup_) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
        mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      stylePopup_ = false;
      render();
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Up) ||
        mappedInput.wasPressed(MappedInputManager::Button::Down)) {
      lineChart_ = !lineChart_;
      stylePopup_ = false;
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
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) close();
}
