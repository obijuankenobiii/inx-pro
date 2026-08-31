#include "BaseHeatmapActivity.h"

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
}  // namespace

void BaseHeatmapActivity::onEnter() { render(); }

void BaseHeatmapActivity::render() {
  renderer.clearScreen();
  const int contentTop = SubPage::header(renderer, "Reading heatmap");
  const int font = systemFontId();
  const int width = renderer.getScreenWidth();
  renderer.rectangle.fill(0, contentTop, width, kRowHeight, static_cast<int>(GfxRenderer::FillTone::Paper));
  renderer.text.render(font, 20, contentTop + (kRowHeight - renderer.text.getLineHeight(font)) / 2, "View", true,
                       EpdFontFamily::REGULAR);
  const char* label = HomeTheme::heatmapViewLabel(view_);
  const int labelWidth = renderer.text.getWidth(font, label);
  renderer.text.render(font, width - labelWidth - 20,
                       contentTop + (kRowHeight - renderer.text.getLineHeight(font)) / 2, label, true,
                       EpdFontFamily::REGULAR);
  if (viewPopup_) renderViewPopup();
  renderer.displayBuffer();
}

void BaseHeatmapActivity::renderViewPopup() {
  const std::vector<std::string> values = {"Daily", "Weekly", "Monthly"};
  const PopUpBounds box = PopUp::bounds(renderer, static_cast<int>(values.size()));
  PopUp::background(renderer, box);
  PopUp::title(renderer, box, "View");
  PopUp::list(renderer, box, values, static_cast<int>(view_), 0);
  PopUp::border(renderer, box);
}

void BaseHeatmapActivity::close() {
  if (onApply_) onApply_(view_);
  if (onBack_) onBack_();
}

void BaseHeatmapActivity::handleTouch(const int x, const int y) {
  if (viewPopup_) {
    const PopUpBounds box = PopUp::bounds(renderer, 3);
    if (!inside(x, y, box.x, box.y, box.width, box.height)) {
      viewPopup_ = false;
      render();
      return;
    }
    const int optionY = y - box.y - box.header;
    if (optionY < 0 || optionY >= box.rows * box.row) return;
    const int selected = optionY / box.row;
    if (selected <= static_cast<int>(HomeTheme::HeatmapView::Monthly)) {
      view_ = static_cast<HomeTheme::HeatmapView>(selected);
      viewPopup_ = false;
      render();
    }
    return;
  }

  const int contentTop = FREEINK_DEVICE_X4PRO ? 80 : 70;
  if (inside(x, y, 0, contentTop, renderer.getScreenWidth(), kRowHeight)) {
    viewPopup_ = true;
    render();
  }
}

void BaseHeatmapActivity::loop() {
  if (viewPopup_) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      viewPopup_ = false;
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
      const int delta = mappedInput.wasPressed(MappedInputManager::Button::Up) ? 2 : 1;
      view_ = static_cast<HomeTheme::HeatmapView>((static_cast<int>(view_) + delta) % 3);
      viewPopup_ = false;
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
    viewPopup_ = true;
    render();
  }
}
