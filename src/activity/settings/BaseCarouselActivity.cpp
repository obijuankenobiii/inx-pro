#include "BaseCarouselActivity.h"

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
}  // namespace

void BaseCarouselActivity::onEnter() { render(); }

void BaseCarouselActivity::render() {
  renderer.clearScreen();
  const int contentTop = SubPage::header(renderer, recentStyle_ ? "Recent" : "Carousel");
  const int font = systemFontId();
  const int width = renderer.getScreenWidth();

  const int styleY = contentTop;
  const int backgroundY = contentTop + kRowHeight;
  const int shadowStyleY = contentTop + kRowHeight * 2;
  const int labelY = contentTop + kRowHeight * 3;
  const int labelColorY = contentTop + kRowHeight * 4;
  renderer.rectangle.fill(0, styleY, width, kRowHeight, static_cast<int>(GfxRenderer::FillTone::Paper));
  renderer.rectangle.fill(0, backgroundY, width, kRowHeight, static_cast<int>(GfxRenderer::FillTone::Paper));
  renderer.rectangle.fill(0, labelY, width, kRowHeight, static_cast<int>(GfxRenderer::FillTone::Paper));
  renderer.rectangle.fill(0, labelColorY, width, kRowHeight, static_cast<int>(GfxRenderer::FillTone::Paper));
  renderer.text.render(font, 20, styleY + (kRowHeight - renderer.text.getLineHeight(font)) / 2, "Style", true,
                       EpdFontFamily::REGULAR);
  const char* styleLabel = recentStyle_ ? (style_ == HomeTheme::CarouselStyle::Right ? "Right" : "Left")
                                        : HomeTheme::carouselStyleLabel(style_);
  const int styleWidth = renderer.text.getWidth(font, styleLabel);
  renderer.text.render(font, width - styleWidth - 20,
                       styleY + (kRowHeight - renderer.text.getLineHeight(font)) / 2, styleLabel, true,
                       EpdFontFamily::REGULAR);
  renderer.text.render(font, 20, backgroundY + (kRowHeight - renderer.text.getLineHeight(font)) / 2, "Background",
                       true, EpdFontFamily::REGULAR);
  Toggle::render(renderer, width - 20, backgroundY, kRowHeight, background_);
  renderer.text.render(font, 20, shadowStyleY + (kRowHeight - renderer.text.getLineHeight(font)) / 2,
                       "Shadow style", true, EpdFontFamily::REGULAR);
  const char* shadowStyleLabel = HomeTheme::carouselShadowStyleLabel(shadowStyle_);
  const int shadowStyleWidth = renderer.text.getWidth(font, shadowStyleLabel);
  renderer.text.render(font, width - shadowStyleWidth - 20,
                       shadowStyleY + (kRowHeight - renderer.text.getLineHeight(font)) / 2, shadowStyleLabel, true,
                       EpdFontFamily::REGULAR);
  renderer.text.render(font, 20, labelY + (kRowHeight - renderer.text.getLineHeight(font)) / 2, "Label", true,
                       EpdFontFamily::REGULAR);
  Toggle::render(renderer, width - 20, labelY, kRowHeight, showLabel_);
  renderer.text.render(font, 20, labelColorY + (kRowHeight - renderer.text.getLineHeight(font)) / 2, "Label color",
                       true, EpdFontFamily::REGULAR);
  const char* labelColor = HomeTheme::carouselLabelColorLabel(labelColor_);
  const int labelColorWidth = renderer.text.getWidth(font, labelColor);
  renderer.text.render(font, width - labelColorWidth - 20,
                       labelColorY + (kRowHeight - renderer.text.getLineHeight(font)) / 2, labelColor, true,
                       EpdFontFamily::REGULAR);
  renderer.line.render(0, backgroundY - 1, width, backgroundY - 1, true, LineRender::Style::Dotted);
  renderer.line.render(0, shadowStyleY - 1, width, shadowStyleY - 1, true, LineRender::Style::Dotted);
  renderer.line.render(0, labelY - 1, width, labelY - 1, true, LineRender::Style::Dotted);
  renderer.line.render(0, labelColorY - 1, width, labelColorY - 1, true, LineRender::Style::Dotted);

  if (stylePopup_) renderStylePopup();
  if (shadowStylePopup_) renderShadowStylePopup();
  if (labelColorPopup_) renderLabelColorPopup();
  renderer.displayBuffer();
}

void BaseCarouselActivity::renderStylePopup() {
  const std::vector<std::string> values = recentStyle_ ? std::vector<std::string>{"Left", "Right"}
                                                       : std::vector<std::string>{"Centered", "Left"};
  const PopUpBounds box = PopUp::bounds(renderer, static_cast<int>(values.size()));
  PopUp::background(renderer, box);
  PopUp::title(renderer, box, "Style");
  const int selected = recentStyle_ ? (style_ == HomeTheme::CarouselStyle::Right ? 1 : 0)
                                    : static_cast<int>(style_);
  PopUp::list(renderer, box, values, selected, 0);
  PopUp::border(renderer, box);
}

void BaseCarouselActivity::renderLabelColorPopup() {
  const std::vector<std::string> values = {"Black", "Gray"};
  const PopUpBounds box = PopUp::bounds(renderer, static_cast<int>(values.size()));
  PopUp::background(renderer, box);
  PopUp::title(renderer, box, "Label color");
  PopUp::list(renderer, box, values, static_cast<int>(labelColor_), 0);
  PopUp::border(renderer, box);
}

void BaseCarouselActivity::renderShadowStylePopup() {
  const std::vector<std::string> values = {"None", "Black", "Gray"};
  const PopUpBounds box = PopUp::bounds(renderer, static_cast<int>(values.size()));
  PopUp::background(renderer, box);
  PopUp::title(renderer, box, "Shadow style");
  PopUp::list(renderer, box, values, static_cast<int>(shadowStyle_), 0);
  PopUp::border(renderer, box);
}

void BaseCarouselActivity::close() {
  if (onApply_) onApply_(style_, background_, showLabel_, labelColor_, shadowStyle_);
  if (onBack_) onBack_();
}

void BaseCarouselActivity::handleTouch(const int x, const int y) {
  if (stylePopup_) {
    const PopUpBounds box = PopUp::bounds(renderer, 2);
    if (!inside(x, y, box.x, box.y, box.width, box.height)) {
      stylePopup_ = false;
      render();
      return;
    }
    const int optionY = y - box.y - box.header;
    if (optionY < 0 || optionY >= box.rows * box.row) return;
    const int selected = optionY / box.row;
    if (selected >= 0 && selected <= 1) {
      style_ = recentStyle_ ? (selected == 0 ? HomeTheme::CarouselStyle::Left : HomeTheme::CarouselStyle::Right)
                            : static_cast<HomeTheme::CarouselStyle>(selected);
      stylePopup_ = false;
      render();
    }
    return;
  }

  if (labelColorPopup_) {
    const PopUpBounds box = PopUp::bounds(renderer, 2);
    if (!inside(x, y, box.x, box.y, box.width, box.height)) {
      labelColorPopup_ = false;
      render();
      return;
    }
    const int optionY = y - box.y - box.header;
    if (optionY < 0 || optionY >= box.rows * box.row) return;
    const int selected = optionY / box.row;
    if (selected >= 0 && selected <= 1) {
      labelColor_ = static_cast<HomeTheme::CarouselLabelColor>(selected);
      labelColorPopup_ = false;
      render();
    }
    return;
  }

  if (shadowStylePopup_) {
    const PopUpBounds box = PopUp::bounds(renderer, 3);
    if (!inside(x, y, box.x, box.y, box.width, box.height)) {
      shadowStylePopup_ = false;
      render();
      return;
    }
    const int optionY = y - box.y - box.header;
    if (optionY < 0 || optionY >= box.rows * box.row) return;
    const int selected = optionY / box.row;
    if (selected >= 0 && selected <= 2) {
      shadowStyle_ = static_cast<HomeTheme::CarouselShadowStyle>(selected);
      shadowStylePopup_ = false;
      render();
    }
    return;
  }

  const int contentTop = FREEINK_DEVICE_X4PRO ? 80 : 70;
  if (x >= 0 && x < renderer.getScreenWidth() && y >= contentTop && y < contentTop + kRowHeight) {
    stylePopup_ = true;
    render();
    return;
  }
  if (x >= 0 && x < renderer.getScreenWidth() && y >= contentTop + kRowHeight &&
      y < contentTop + kRowHeight * 2) {
    background_ = !background_;
    render();
    return;
  }
  if (x >= 0 && x < renderer.getScreenWidth() && y >= contentTop + kRowHeight * 2 &&
      y < contentTop + kRowHeight * 3) {
    shadowStylePopup_ = true;
    render();
    return;
  }
  if (x >= 0 && x < renderer.getScreenWidth() && y >= contentTop + kRowHeight * 3 &&
      y < contentTop + kRowHeight * 4) {
    showLabel_ = !showLabel_;
    render();
    return;
  }
  if (x >= 0 && x < renderer.getScreenWidth() && y >= contentTop + kRowHeight * 4 &&
      y < contentTop + kRowHeight * 5) {
    labelColorPopup_ = true;
    render();
  }
}

void BaseCarouselActivity::loop() {
  if (stylePopup_) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
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
    if (mappedInput.wasPressed(MappedInputManager::Button::Up) ||
        mappedInput.wasPressed(MappedInputManager::Button::Down)) {
      if (recentStyle_) {
        style_ = style_ == HomeTheme::CarouselStyle::Left ? HomeTheme::CarouselStyle::Right
                                                           : HomeTheme::CarouselStyle::Left;
      } else {
        style_ = style_ == HomeTheme::CarouselStyle::Centered ? HomeTheme::CarouselStyle::Left
                                                               : HomeTheme::CarouselStyle::Centered;
      }
      stylePopup_ = false;
      render();
    }
    return;
  }

  if (shadowStylePopup_) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      shadowStylePopup_ = false;
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
      shadowStyle_ = static_cast<HomeTheme::CarouselShadowStyle>(
          (static_cast<int>(shadowStyle_) + delta) % 3);
      shadowStylePopup_ = false;
      render();
    }
    return;
  }

  if (labelColorPopup_) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      labelColorPopup_ = false;
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
      labelColor_ = labelColor_ == HomeTheme::CarouselLabelColor::Black
                        ? HomeTheme::CarouselLabelColor::Gray
                        : HomeTheme::CarouselLabelColor::Black;
      labelColorPopup_ = false;
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
    stylePopup_ = true;
    render();
    return;
  }
}
