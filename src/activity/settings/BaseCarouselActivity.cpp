#include "BaseCarouselActivity.h"

#include <GfxRenderer.h>

#include <algorithm>
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

void BaseCarouselActivity::onEnter() { render(); }

void BaseCarouselActivity::render() {
  renderer.clearScreen();
  const int contentTop = contentTop_ = SubPage::header(renderer, recentStyle_ ? "Recent" : "Carousel");
  const int rowHeight = kRowHeight;
  const int font = systemFontId();
  const int width = renderer.getScreenWidth();
  const int itemCount = recentStyle_ ? 9 : 6;
  const int visibleRows = std::max(1, (renderer.getScreenHeight() - contentTop) / rowHeight);
  scrollOffset_ = std::max(0, std::min(scrollOffset_, itemCount - visibleRows));
  const int lastItem = std::min(itemCount, scrollOffset_ + visibleRows);
  const int lineHeight = renderer.text.getLineHeight(font);
  for (int item = scrollOffset_; item < lastItem; ++item) {
    const int rowY = contentTop + (item - scrollOffset_) * rowHeight;
    renderer.rectangle.fill(0, rowY, width, rowHeight, static_cast<int>(GfxRenderer::FillTone::Paper));
    const int textY = rowY + (rowHeight - lineHeight) / 2;
    switch (item) {
      case 0: {
        renderer.text.render(font, 20, textY, "Style", true, EpdFontFamily::REGULAR);
        const char* value = recentStyle_ ? (style_ == HomeTheme::CarouselStyle::Right ? "Right" : "Left")
                                         : HomeTheme::carouselStyleLabel(style_);
        renderer.text.render(font, width - renderer.text.getWidth(font, value) - 20, textY, value, true,
                             EpdFontFamily::REGULAR);
        break;
      }
      case 1:
        renderer.text.render(font, 20, textY, "Background", true, EpdFontFamily::REGULAR);
        Toggle::render(renderer, width - 20, rowY, rowHeight, background_);
        break;
      case 2: {
        renderer.text.render(font, 20, textY, "Shadow style", true, EpdFontFamily::REGULAR);
        const char* value = HomeTheme::carouselShadowStyleLabel(shadowStyle_);
        renderer.text.render(font, width - renderer.text.getWidth(font, value) - 20, textY, value, true,
                             EpdFontFamily::REGULAR);
        break;
      }
      case 3:
        renderer.text.render(font, 20, textY, "Label", true, EpdFontFamily::REGULAR);
        Toggle::render(renderer, width - 20, rowY, rowHeight, showLabel_);
        break;
      case 4: {
        renderer.text.render(font, 20, textY, "Label color", true, EpdFontFamily::REGULAR);
        const char* value = HomeTheme::carouselLabelColorLabel(labelColor_);
        renderer.text.render(font, width - renderer.text.getWidth(font, value) - 20, textY, value, true,
                             EpdFontFamily::REGULAR);
        break;
      }
      case 5:
        if (recentStyle_) {
          renderer.text.render(font, 20, textY, "Show title", true, EpdFontFamily::REGULAR);
          Toggle::render(renderer, width - 20, rowY, rowHeight, showTitle_);
        } else {
          renderer.text.render(font, 20, textY, "Show progress", true, EpdFontFamily::REGULAR);
          Toggle::render(renderer, width - 20, rowY, rowHeight, showProgress_);
        }
        break;
      case 6:
        renderer.text.render(font, 20, textY, "Show author", true, EpdFontFamily::REGULAR);
        Toggle::render(renderer, width - 20, rowY, rowHeight, showAuthor_);
        break;
      case 7:
        renderer.text.render(font, 20, textY, "Show rating", true, EpdFontFamily::REGULAR);
        Toggle::render(renderer, width - 20, rowY, rowHeight, showRating_);
        break;
      case 8:
        renderer.text.render(font, 20, textY, "Show progress", true, EpdFontFamily::REGULAR);
        Toggle::render(renderer, width - 20, rowY, rowHeight, showProgress_);
        break;
    }
    if (item > scrollOffset_) {
      renderer.line.render(0, rowY - 1, width, rowY - 1, true, LineRender::Style::Dotted);
    }
  }
  if (itemCount > visibleRows) {
    const int trackTop = contentTop + 4;
    const int trackHeight = std::max(1, visibleRows * rowHeight - 8);
    const int thumbHeight = std::max(18, trackHeight * visibleRows / itemCount);
    const int thumbRange = std::max(0, trackHeight - thumbHeight);
    const int thumbY = trackTop + (itemCount > visibleRows ? thumbRange * scrollOffset_ /
                                                               (itemCount - visibleRows)
                                                         : 0);
    renderer.line.render(width - 4, trackTop, width - 4, trackTop + trackHeight, true,
                         LineRender::Style::Dotted);
    renderer.line.render(width - 4, thumbY, width - 4, thumbY + thumbHeight, true);
  }

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
  if (onApply_) {
    onApply_(style_, background_, showLabel_, labelColor_, shadowStyle_, showTitle_, showAuthor_, showProgress_,
             showRating_);
  }
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

  if (x < 0 || x >= renderer.getScreenWidth() || y < contentTop_ || y >= renderer.getScreenHeight()) return;
  const int visibleIndex = (y - contentTop_) / kRowHeight;
  const int visibleRows = std::max(1, (renderer.getScreenHeight() - contentTop_) / kRowHeight);
  if (visibleIndex >= visibleRows) return;
  const int item = scrollOffset_ + visibleIndex;
  if ((!recentStyle_ && item >= 6) || (recentStyle_ && item >= 9)) return;

  switch (item) {
    case 0:
      stylePopup_ = true;
      break;
    case 1:
      background_ = !background_;
      break;
    case 2:
      shadowStylePopup_ = true;
      break;
    case 3:
      showLabel_ = !showLabel_;
      break;
    case 4:
      labelColorPopup_ = true;
      break;
    case 5:
      if (recentStyle_) {
        showTitle_ = !showTitle_;
      } else {
        showProgress_ = !showProgress_;
      }
      break;
    case 6:
      showAuthor_ = !showAuthor_;
      break;
    case 7:
      showRating_ = !showRating_;
      break;
    case 8:
      showProgress_ = !showProgress_;
      break;
  }
  render();
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

  if (SubPage::closeInput(renderer, mappedInput, [this] { close(); }, false)) return;

  const int itemCount = recentStyle_ ? 9 : 6;
  const int visibleRows = std::max(1, (renderer.getScreenHeight() - contentTop_) / kRowHeight);
  const int maxScroll = std::max(0, itemCount - visibleRows);
  if (mappedInput.hasTouch() && mappedInput.wasTouchSwipeUpForRenderer(renderer)) {
    if (scrollOffset_ < maxScroll) {
      ++scrollOffset_;
      render();
    }
    return;
  }
  if (mappedInput.hasTouch() && mappedInput.wasTouchSwipeDownForRenderer(renderer)) {
    if (scrollOffset_ > 0) {
      --scrollOffset_;
      render();
    }
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Up) && scrollOffset_ > 0) {
    --scrollOffset_;
    render();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Down) && scrollOffset_ < maxScroll) {
    ++scrollOffset_;
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
