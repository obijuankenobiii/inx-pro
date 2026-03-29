#include "ThemePickerActivity.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <string>
#include <vector>

#include "activity/page/SubPage.h"
#include "activity/page/components/global/PopUp.h"
#include "activity/settings/BaseCarouselActivity.h"
#include "images/ThemeBorder.h"
#include "images/Setting.h"
#include "state/HomeTheme.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"

namespace {

constexpr int kRowHeight = Page::LIST_ITEM_HEIGHT;
constexpr int kTop = 80;
constexpr int kPreviewTop = 110;

struct PreviewBounds {
  int x;
  int y;
  int width;
  int height;
};

PreviewBounds widgetPreviewBounds(const GfxRenderer& renderer) {
  const int homeWidth = renderer.getScreenWidth();
  const int homeHeight = renderer.getScreenHeight() - navigation::Menu::height - navigation::Menu::bottomHeight;
  const int availableHeight = std::max(1, renderer.getScreenHeight() - kPreviewTop - 20);
  const int previewHeight = std::min(homeHeight, availableHeight);
  const int previewWidth = std::max(1, homeWidth * previewHeight / std::max(1, homeHeight));
  return {(renderer.getScreenWidth() - previewWidth) / 2, kPreviewTop, previewWidth, previewHeight};
}

bool supportsCarouselSettings(const HomeTheme::Widget widget) {
  return widget == HomeTheme::Widget::Carousel || widget == HomeTheme::Widget::Recent ||
         widget == HomeTheme::Widget::Favorites;
}

struct CarouselSettingsBounds {
  int x;
  int y;
  int size;
};

CarouselSettingsBounds carouselSettingsBounds(const int cellX, const int cellY, const int cellW, const int cellH) {
  constexpr int size = 40;
  (void)cellW;
  return {cellX + 8, cellY + std::max(0, cellH - size - 8), size};
}

int widgetOptionCount(const HomeTheme::Layout layout, const bool sleepTheme) {
  if (layout == HomeTheme::Layout::Classic) return 1;
#if FREEINK_DEVICE_STICKY
  return sleepTheme ? 5 : 11;
#else
  return sleepTheme ? 3 : 9;
#endif
}

HomeTheme::Widget widgetOptionAt(const bool sleepTheme, const int index) {
  static constexpr HomeTheme::Widget homeOptions[] = {
      HomeTheme::Widget::Empty,
      HomeTheme::Widget::Carousel,
      HomeTheme::Widget::Shortcuts,
      HomeTheme::Widget::Clock,
      HomeTheme::Widget::Recent,
#if FREEINK_DEVICE_STICKY
      HomeTheme::Widget::Temperature,
#endif
      HomeTheme::Widget::Calendar,
      HomeTheme::Widget::ListShortcuts,
#if FREEINK_DEVICE_STICKY
      HomeTheme::Widget::Humidity,
#endif
    HomeTheme::Widget::TodaysReading,
    HomeTheme::Widget::Favorites,
  };
  static constexpr HomeTheme::Widget sleepOptions[] = {
      HomeTheme::Widget::Empty,
      HomeTheme::Widget::Clock,
      HomeTheme::Widget::Calendar,
#if FREEINK_DEVICE_STICKY
      HomeTheme::Widget::Temperature,
      HomeTheme::Widget::Humidity,
#endif
  };
  const HomeTheme::Widget* options = sleepTheme ? sleepOptions : homeOptions;
  const int optionCount = sleepTheme ? static_cast<int>(sizeof(sleepOptions) / sizeof(sleepOptions[0]))
                                     : static_cast<int>(sizeof(homeOptions) / sizeof(homeOptions[0]));
  if (index < 0 || index >= optionCount) return HomeTheme::Widget::Empty;
  return options[index];
}

int widgetOptionIndex(const bool sleepTheme, const HomeTheme::Widget widget) {
  const int count = widgetOptionCount(HomeTheme::Layout::OneByTwo, sleepTheme);
  for (int i = 0; i < count; ++i) {
    if (widgetOptionAt(sleepTheme, i) == widget) return i;
  }
  return 0;
}

struct BorderIconBounds {
  int x;
  int y;
  int size;
};

BorderIconBounds borderIconBounds(const int cellX, const int cellY, const int cellW, const int cellH) {
  constexpr int size = 40;
  const int dividerY = cellY + cellH;
  return {cellX + std::max(0, cellW - size - 6), dividerY - size / 2, size};
}

bool inside(const int x, const int y, const int left, const int top, const int width, const int height) {
  return x >= left && x < left + width && y >= top && y < top + height;
}

void drawLayoutDiagram(GfxRenderer& renderer, const HomeTheme::Layout layout, const int x, const int y, const int w,
                       const int h, const bool selected) {
  (void)selected;
  renderer.rectangle.fill(x, y, w, h, false);
  renderer.rectangle.render(x, y, w, h, true);
  const int inset = 12;
  const int gap = 8;
  const int columns = layout == HomeTheme::Layout::TwoByTwo ? 2 : 1;
  const int rows = 2;
  const int cellW = (w - inset * 2 - gap * (columns - 1)) / columns;
  const int cellH = (h - inset * 2 - gap * (rows - 1)) / rows;
  for (int i = 0; i < columns * rows; ++i) {
    const int cellX = x + inset + (i % columns) * (cellW + gap);
    const int cellY = y + inset + (i / columns) * (cellH + gap);
    renderer.rectangle.fill(cellX, cellY, cellW, cellH, false);
    renderer.rectangle.render(cellX, cellY, cellW, cellH, true);
  }
}

}  // namespace

void ThemePickerActivity::onEnter() {
  ActivityWithSubactivity::onEnter();
  HomeTheme::load();
  selected_ = openSleepTheme_ ? HomeTheme::count() : HomeTheme::activeIndex();
  screen_ = Screen::List;
  if (openSleepTheme_) {
    editTheme();
  } else {
    render();
  }
}

void ThemePickerActivity::close() {
  if (onBack_) onBack_();
}

void ThemePickerActivity::render() {
  renderer.clearScreen();
  if (screen_ == Screen::List) {
    renderList();
  } else if (screen_ == Screen::Layout) {
    renderLayoutPicker();
  } else {
    renderWidgetPicker();
  }
  if (widgetPopup_) {
    renderWidgetPopup();
  }
  if (borderPopup_) {
    renderBorderPopup();
  }
  renderer.displayBuffer();
}

void ThemePickerActivity::renderList() {
  const int contentTop = SubPage::header(renderer, "Theme");
  const int font = systemFontId();
  const int width = renderer.getScreenWidth();
  const int themeCount = HomeTheme::count() + 1;
  for (int i = 0; i < themeCount; ++i) {
    const int y = contentTop + i * kRowHeight;
    const bool sleepTheme = i == HomeTheme::count();
    const HomeTheme::Theme& theme = sleepTheme ? HomeTheme::sleep() : HomeTheme::at(i);
    renderer.rectangle.fill(0, y, width, kRowHeight,
                            static_cast<int>(GfxRenderer::FillTone::Paper));
    renderer.text.render(font, 20, y + (kRowHeight - renderer.text.getLineHeight(font)) / 2,
                         theme.name, true, EpdFontFamily::REGULAR);
    const char* layout = HomeTheme::layoutLabel(theme.layout);
    renderer.text.render(font, width - renderer.text.getWidth(font, layout) - 24,
                         y + (kRowHeight - renderer.text.getLineHeight(font)) / 2, layout, true,
                         EpdFontFamily::REGULAR);
    if (i + 1 < themeCount) {
      renderer.line.render(0, y + kRowHeight - 1, width, y + kRowHeight - 1, true,
                           LineRender::Style::Dotted);
    }
  }

}

void ThemePickerActivity::renderLayoutPicker() {
  SubPage::header(renderer, "Choose layout");
  const int w = (renderer.getScreenWidth() - 60) / 2;
  const int h = 300;
  drawLayoutDiagram(renderer, HomeTheme::Layout::OneByTwo, 20, 110, w, h, layout_ == HomeTheme::Layout::OneByTwo);
  drawLayoutDiagram(renderer, HomeTheme::Layout::TwoByTwo, 40 + w, 110, w, h, layout_ == HomeTheme::Layout::TwoByTwo);
  const int font = systemFontId();
  renderer.text.centered(font, 430, "Tap a layout to continue");
}

void ThemePickerActivity::renderWidgetPicker() {
  SubPage::header(renderer, "Choose widget");
  const PreviewBounds preview = widgetPreviewBounds(renderer);
  const int previewX = preview.x;
  const int previewY = preview.y;
  const int previewW = preview.width;
  const int previewH = preview.height;
  renderer.rectangle.fill(previewX, previewY, previewW, previewH, false);

  const int columns = layout_ == HomeTheme::Layout::TwoByTwo ? 2 : 1;
  const int rows = 2;
  const int cellW = previewW / columns;
  const int cellH = previewH / rows;
  for (int slot = 0; slot < HomeTheme::slotCount(layout_); ++slot) {
    const int cellX = previewX + (slot % columns) * cellW;
    const int cellY = previewY + (slot / columns) * cellH;
    renderWidgetPreview(widgets_[slot], cellX, cellY, cellW, cellH, backgrounds_[slot] != 0,
                        carouselStyles_[slot], carouselLabels_[slot] != 0, carouselLabelColors_[slot],
                        carouselShadowStyles_[slot]);
  }

  renderBorder(borders_[0], previewX, previewY, previewW, previewH);
  const BorderIconBounds icon = borderIconBounds(previewX, previewY, previewW, cellH);
  renderer.rectangle.fill(icon.x - 3, icon.y - 3, icon.size + 6, icon.size + 6, false);
  renderer.rectangle.render(icon.x - 3, icon.y - 3, icon.size + 6, icon.size + 6, true);
  renderer.icon.render(ThemeBorder, icon.x, icon.y, icon.size, icon.size);

  for (int slot = 0; slot < HomeTheme::slotCount(layout_); ++slot) {
    const int cellX = previewX + (slot % columns) * cellW;
    const int cellY = previewY + (slot / columns) * cellH;
    if (supportsCarouselSettings(widgets_[slot])) {
      const CarouselSettingsBounds settings = carouselSettingsBounds(cellX, cellY, cellW, cellH);
      renderer.rectangle.fill(settings.x - 4, settings.y - 4, settings.size + 8, settings.size + 8, false);
      renderer.rectangle.render(settings.x - 4, settings.y - 4, settings.size + 8, settings.size + 8, true);
      renderer.bitmap.icon(Setting, settings.x, settings.y, settings.size, settings.size);
    }
  }
}

void ThemePickerActivity::renderBorder(const HomeTheme::Border border, const int x, const int y, const int width,
                                       const int height) {
  if (width <= 0 || height <= 0) return;
  const int columns = layout_ == HomeTheme::Layout::TwoByTwo ? 2 : 1;
  const int cellW = width / columns;
  const int cellH = height / 2;
  switch (border) {
    case HomeTheme::Border::Subtle:
      renderer.line.render(x, y + cellH, x + width, y + cellH, true, LineRender::Style::Dotted);
      if (columns == 2) renderer.line.render(x + cellW, y, x + cellW, y + height, true, LineRender::Style::Dotted);
      break;
    case HomeTheme::Border::Normal:
      renderer.line.render(x, y + cellH, x + width, y + cellH, true);
      if (columns == 2) renderer.line.render(x + cellW, y, x + cellW, y + height, true);
      break;
    case HomeTheme::Border::Thick:
      for (int offset = -1; offset <= 1; ++offset) {
        renderer.line.render(x, y + cellH + offset, x + width, y + cellH + offset, true);
        if (columns == 2) renderer.line.render(x + cellW + offset, y, x + cellW + offset, y + height, true);
      }
      break;
    case HomeTheme::Border::None:
    default:
      break;
  }
}

void ThemePickerActivity::renderWidgetPreview(const HomeTheme::Widget widget, const int x, const int y,
                                              const int width, const int height, const bool background,
                                              const HomeTheme::CarouselStyle style, const bool showLabel,
                                              const HomeTheme::CarouselLabelColor labelColor,
                                              const HomeTheme::CarouselShadowStyle shadowStyle) {
  if (width <= 0 || height <= 0) return;

  switch (widget) {
    case HomeTheme::Widget::Carousel:
      carousel_.preview(x, y, width, height, background, style, showLabel, labelColor, shadowStyle);
      break;
    case HomeTheme::Widget::Recent:
      recent_.preview(x, y, width, height, background, style, showLabel, labelColor, shadowStyle);
      break;
    case HomeTheme::Widget::Shortcuts:
      shortcut_.render(x, y, width, height);
      break;
    case HomeTheme::Widget::ListShortcuts:
      shortcutList_.render(x, y, width, height);
      break;
    case HomeTheme::Widget::Clock:
      clock_.render(x, y, width, height);
      break;
    case HomeTheme::Widget::Calendar:
      calendar_.render(x, y, width, height);
      break;
#if FREEINK_DEVICE_STICKY
    case HomeTheme::Widget::Temperature:
      temperature_.preview(x, y, width, height);
      break;
    case HomeTheme::Widget::Humidity:
      humidity_.preview(x, y, width, height);
      break;
#endif
    case HomeTheme::Widget::TodaysReading:
      todaysReading_.preview(x, y, width, height);
      break;
    case HomeTheme::Widget::Favorites:
      favorites_.preview(x, y, width, height, background, style, showLabel, labelColor, shadowStyle);
      break;
    case HomeTheme::Widget::Empty:
    default:
      renderer.rectangle.fill(x, y, width, height, false);
      {
        const int labelFont = systemFontId();
        const int plusFont = MONTSERRAT_16_FONT_ID;
        const int gap = 6;
        const int labelWidth = renderer.text.getWidth(labelFont, "Add widget");
        const int plusWidth = renderer.text.getWidth(plusFont, "+");
        const int startX = x + std::max(0, (width - labelWidth - gap - plusWidth) / 2);
        const int labelY = y + (height - renderer.text.getLineHeight(labelFont)) / 2;
        const int plusY = y + (height - renderer.text.getLineHeight(plusFont)) / 2;
        renderer.text.render(labelFont, startX, labelY, "Add widget", true, EpdFontFamily::REGULAR);
        renderer.text.render(plusFont, startX + labelWidth + gap, plusY, "+", true, EpdFontFamily::BOLD);
      }
      break;
  }
}

void ThemePickerActivity::renderWidgetPopup() {
  const int options = widgetOptionCount(layout_, editingSleep_);
  std::vector<std::string> values;
  values.reserve(static_cast<size_t>(options));
  for (int i = 0; i < options; ++i) {
    values.emplace_back(HomeTheme::widgetLabel(widgetOptionAt(editingSleep_, i)));
  }
  const PopUpBounds box = PopUp::bounds(renderer, options);
  PopUp::background(renderer, box);
  PopUp::title(renderer, box, "Choose widget");
  PopUp::list(renderer, box, values, popupSelected_, widgetPopupScroll_);
  PopUp::border(renderer, box);
}

void ThemePickerActivity::renderBorderPopup() {
  static const char* const labels[] = {"None", "Subtle", "Normal", "Thick"};
  std::vector<std::string> values;
  for (const char* label : labels) values.emplace_back(label);
  const PopUpBounds box = PopUp::bounds(renderer, static_cast<int>(values.size()));
  PopUp::background(renderer, box);
  PopUp::title(renderer, box, "Border");
  PopUp::list(renderer, box, values, borderPopupSelected_, 0);
  PopUp::border(renderer, box);
}

void ThemePickerActivity::editTheme() {
  editingSleep_ = selected_ == HomeTheme::count();
  const HomeTheme::Theme& theme = editingSleep_ ? HomeTheme::sleep() : HomeTheme::at(selected_);
  if (!editingSleep_) HomeTheme::activate(selected_);
  layout_ = theme.layout;
  for (int i = 0; i < 4; ++i) {
    widgets_[i] = theme.widgets[i];
    borders_[i] = theme.borders[i];
    backgrounds_[i] = theme.backgrounds[i];
    carouselStyles_[i] = theme.carouselStyles[i];
    carouselLabels_[i] = theme.carouselLabels[i];
    carouselLabelColors_[i] = theme.carouselLabelColors[i];
    carouselShadowStyles_[i] = theme.carouselShadowStyles[i];
#if !FREEINK_DEVICE_STICKY
    if (widgets_[i] == HomeTheme::Widget::Temperature || widgets_[i] == HomeTheme::Widget::Humidity) {
      widgets_[i] = HomeTheme::Widget::Empty;
      borders_[i] = HomeTheme::Border::None;
      carouselStyles_[i] = HomeTheme::CarouselStyle::Centered;
      carouselLabels_[i] = 0;
      carouselLabelColors_[i] = HomeTheme::CarouselLabelColor::Black;
      carouselShadowStyles_[i] = HomeTheme::CarouselShadowStyle::None;
    }
#endif
  }
  if (layout_ == HomeTheme::Layout::Classic) {
    for (HomeTheme::Widget& widget : widgets_) widget = HomeTheme::Widget::Empty;
    for (HomeTheme::Border& border : borders_) border = HomeTheme::Border::None;
    for (HomeTheme::CarouselStyle& style : carouselStyles_) style = HomeTheme::CarouselStyle::Centered;
    for (uint8_t& label : carouselLabels_) label = 0;
    for (HomeTheme::CarouselLabelColor& color : carouselLabelColors_) color = HomeTheme::CarouselLabelColor::Black;
    for (HomeTheme::CarouselShadowStyle& style : carouselShadowStyles_) {
      style = HomeTheme::CarouselShadowStyle::None;
    }
  }
  widgetSlot_ = 0;
  editingExisting_ = true;
  screen_ = Screen::Widgets;
  render();
}

void ThemePickerActivity::saveEditorAndClose() {
  if (screen_ == Screen::Widgets && editingExisting_ && !widgetPopup_ && !borderPopup_) {
    if (editingSleep_) {
      HomeTheme::updateSleep(layout_, widgets_, borders_, backgrounds_, carouselStyles_, carouselLabels_,
                             carouselLabelColors_, carouselShadowStyles_,
                             HomeTheme::slotCount(layout_));
    } else {
      HomeTheme::update(selected_, layout_, widgets_, borders_, backgrounds_, carouselStyles_, carouselLabels_,
                        carouselLabelColors_, carouselShadowStyles_,
                        HomeTheme::slotCount(layout_));
    }
  }
  close();
}

void ThemePickerActivity::openWidgetPopup(const int slot) {
  widgetSlot_ = slot;
  popupSelected_ = widgetOptionIndex(editingSleep_, widgets_[slot]);
  const int options = widgetOptionCount(layout_, editingSleep_);
  const int visibleRows = std::min(PopUp::maxRows, options);
  widgetPopupScroll_ = std::max(0, std::min(popupSelected_ - visibleRows + 1, options - visibleRows));
  widgetPopup_ = true;
  render();
}

void ThemePickerActivity::openBorderPopup(const int slot) {
  widgetSlot_ = slot;
  borderPopupSelected_ = static_cast<int>(borders_[slot]);
  borderPopup_ = true;
  render();
}

void ThemePickerActivity::openCarouselSettings(const int slot) {
  widgetSlot_ = slot;
  carouselSettingsFinished_ = false;
  enterNewActivity(new BaseCarouselActivity(
      renderer, mappedInput, carouselStyles_[slot], backgrounds_[slot], carouselLabels_[slot] != 0,
      carouselLabelColors_[slot], carouselShadowStyles_[slot],
      [this, slot](const HomeTheme::CarouselStyle style, const bool background, const bool showLabel,
                   const HomeTheme::CarouselLabelColor labelColor,
                   const HomeTheme::CarouselShadowStyle shadowStyle) {
        carouselStyles_[slot] = style;
        backgrounds_[slot] = background ? 1 : 0;
        carouselLabels_[slot] = showLabel ? 1 : 0;
        carouselLabelColors_[slot] = labelColor;
        carouselShadowStyles_[slot] = shadowStyle;
        carouselSettingsFinished_ = true;
      },
      [] {}, widgets_[slot] == HomeTheme::Widget::Recent));
}

void ThemePickerActivity::moveWidgetPopupSelection(const int delta) {
  const int options = widgetOptionCount(layout_, editingSleep_);
  if (options <= 0) return;
  popupSelected_ = (popupSelected_ + options + delta) % options;
  const int visibleRows = std::min(PopUp::maxRows, options);
  const int maxScroll = std::max(0, options - visibleRows);
  if (popupSelected_ < widgetPopupScroll_) widgetPopupScroll_ = popupSelected_;
  if (popupSelected_ >= widgetPopupScroll_ + visibleRows) {
    widgetPopupScroll_ = popupSelected_ - visibleRows + 1;
  }
  widgetPopupScroll_ = std::max(0, std::min(widgetPopupScroll_, maxScroll));
}

void ThemePickerActivity::pageWidgetPopup(const int delta) {
  const int options = widgetOptionCount(layout_, editingSleep_);
  const int visibleRows = std::min(PopUp::maxRows, options);
  const int maxScroll = std::max(0, options - visibleRows);
  widgetPopupScroll_ = std::max(0, std::min(widgetPopupScroll_ + delta * visibleRows, maxScroll));
  if (popupSelected_ < widgetPopupScroll_) popupSelected_ = widgetPopupScroll_;
  if (popupSelected_ >= widgetPopupScroll_ + visibleRows) {
    popupSelected_ = widgetPopupScroll_ + visibleRows - 1;
  }
}

void ThemePickerActivity::handleTouch(const int x, const int y) {
  if (screen_ == Screen::List) {
    const int themeCount = HomeTheme::count() + 1;
    if (y >= kTop && y < kTop + themeCount * kRowHeight) {
      selected_ = (y - kTop) / kRowHeight;
      editTheme();
      return;
    }
  } else if (screen_ == Screen::Layout) {
    const int w = (renderer.getScreenWidth() - 60) / 2;
    if (inside(x, y, 20, 110, w, 300)) layout_ = HomeTheme::Layout::OneByTwo;
    else if (inside(x, y, 40 + w, 110, w, 300)) layout_ = HomeTheme::Layout::TwoByTwo;
    else return;
    screen_ = Screen::Widgets;
    widgetSlot_ = 0;
    render();
    return;
  } else {
    if (borderPopup_) {
      const PopUpBounds box = PopUp::bounds(renderer, 4);
      if (!inside(x, y, box.x, box.y, box.width, box.height)) {
        borderPopup_ = false;
        render();
        return;
      }
      const int optionY = y - box.y - box.header;
      if (optionY < 0 || optionY >= box.rows * box.row) return;
      const int selected = optionY / box.row;
      if (selected >= 4) return;
      borders_[widgetSlot_] = static_cast<HomeTheme::Border>(selected);
      borderPopup_ = false;
      render();
      return;
    }
    if (widgetPopup_) {
      const int options = widgetOptionCount(layout_, editingSleep_);
      const PopUpBounds box = PopUp::bounds(renderer, options);
      if (!inside(x, y, box.x, box.y, box.width, box.height)) {
        widgetPopup_ = false;
        render();
        return;
      }
      const int optionY = y - box.y - box.header;
      if (optionY < 0 || optionY >= box.rows * box.row) return;
      popupSelected_ = widgetPopupScroll_ + optionY / box.row;
      if (popupSelected_ >= options) return;
      widgets_[widgetSlot_] = widgetOptionAt(editingSleep_, popupSelected_);
      if (widgets_[widgetSlot_] == HomeTheme::Widget::Empty) borders_[widgetSlot_] = HomeTheme::Border::None;
      if (supportsCarouselSettings(widgets_[widgetSlot_])) {
        carouselStyles_[widgetSlot_] = HomeTheme::defaultCarouselStyle(widgets_[widgetSlot_]);
        carouselLabels_[widgetSlot_] = 1;
        carouselLabelColors_[widgetSlot_] = HomeTheme::CarouselLabelColor::Black;
        carouselShadowStyles_[widgetSlot_] = HomeTheme::CarouselShadowStyle::None;
      } else {
        carouselStyles_[widgetSlot_] = HomeTheme::CarouselStyle::Centered;
        carouselLabels_[widgetSlot_] = 0;
        carouselLabelColors_[widgetSlot_] = HomeTheme::CarouselLabelColor::Black;
        carouselShadowStyles_[widgetSlot_] = HomeTheme::CarouselShadowStyle::None;
      }
      widgetPopup_ = false;
      render();
      return;
    }
    const PreviewBounds preview = widgetPreviewBounds(renderer);
    const int previewX = preview.x;
    const int previewY = preview.y;
    const int previewW = preview.width;
    const int previewH = preview.height;
    const int columns = layout_ == HomeTheme::Layout::TwoByTwo ? 2 : 1;
    const int rows = 2;
    const int cellW = previewW / columns;
    const int cellH = previewH / rows;
    for (int slot = 0; slot < HomeTheme::slotCount(layout_); ++slot) {
      const int cellX = previewX + (slot % columns) * cellW;
      const int cellY = previewY + (slot / columns) * cellH;
      if (supportsCarouselSettings(widgets_[slot])) {
        const CarouselSettingsBounds settings = carouselSettingsBounds(cellX, cellY, cellW, cellH);
        if (inside(x, y, settings.x - 8, settings.y - 8, settings.size + 16, settings.size + 16)) {
          openCarouselSettings(slot);
          return;
        }
      }
    }
    if (inside(x, y, previewX, previewY, previewW, previewH)) {
      const int column = std::min(columns - 1, (x - previewX) / cellW);
      const int row = std::min(1, (y - previewY) / cellH);
      const int slot = row * columns + column;
      if (slot < HomeTheme::slotCount(layout_)) {
        const BorderIconBounds icon = borderIconBounds(previewX, previewY, previewW, cellH);
        if (inside(x, y, icon.x - 4, icon.y - 4, icon.size + 8, icon.size + 8)) {
          openBorderPopup(0);
        } else {
          openWidgetPopup(slot);
        }
      }
      return;
    }
  }
}

void ThemePickerActivity::loop() {
  if (subActivity) {
    subActivity->loop();
    if (carouselSettingsFinished_) {
      exitActivity();
      carouselSettingsFinished_ = false;
      render();
    }
    return;
  }
  if (screen_ == Screen::Widgets && widgetPopup_ && mappedInput.hasTouch()) {
    if (mappedInput.wasTouchSwipeUp()) {
      pageWidgetPopup(1);
      render();
      return;
    }
    if (mappedInput.wasTouchSwipeDown()) {
      pageWidgetPopup(-1);
      render();
      return;
    }
  }
  if (!widgetPopup_ && !borderPopup_ && SubPage::closeInput(renderer, mappedInput, [this] { saveEditorAndClose(); })) {
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (widgetPopup_) {
      widgetPopup_ = false;
      render();
      return;
    }
    if (borderPopup_) {
      borderPopup_ = false;
      render();
      return;
    }
    return;
  }
  if (mappedInput.hasTouch()) {
    float nx = 0.0f;
    float ny = 0.0f;
    if (mappedInput.wasTouchTapInScreen(renderer, nx, ny)) {
      const int tapX = static_cast<int>(nx * renderer.getScreenWidth());
      const int tapY = static_cast<int>(ny * renderer.getScreenHeight());
      handleTouch(tapX, tapY);
      return;
    }
  }
  if (screen_ == Screen::List) {
    const int themeCount = HomeTheme::count() + 1;
    if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
      selected_ = (selected_ + themeCount - 1) % themeCount;
      render();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
      selected_ = (selected_ + 1) % themeCount;
      render();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      editTheme();
    }
    return;
  }
  if (screen_ == Screen::Widgets) {
    if (borderPopup_) {
      const PopUpBounds box = PopUp::bounds(renderer, 4);
      if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
        borderPopupSelected_ = (borderPopupSelected_ + 3) % 4;
        render();
      } else if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
        borderPopupSelected_ = (borderPopupSelected_ + 1) % 4;
        render();
      } else if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
        borders_[widgetSlot_] = static_cast<HomeTheme::Border>(borderPopupSelected_);
        borderPopup_ = false;
        render();
      }
      (void)box;
      return;
    }
    if (widgetPopup_) {
      if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
        moveWidgetPopupSelection(-1);
        render();
      } else if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
        moveWidgetPopupSelection(1);
        render();
      } else if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
        widgets_[widgetSlot_] = widgetOptionAt(editingSleep_, popupSelected_);
        if (widgets_[widgetSlot_] == HomeTheme::Widget::Empty) borders_[widgetSlot_] = HomeTheme::Border::None;
        if (supportsCarouselSettings(widgets_[widgetSlot_])) {
          carouselStyles_[widgetSlot_] = HomeTheme::defaultCarouselStyle(widgets_[widgetSlot_]);
        carouselLabels_[widgetSlot_] = 1;
        carouselLabelColors_[widgetSlot_] = HomeTheme::CarouselLabelColor::Black;
        carouselShadowStyles_[widgetSlot_] = HomeTheme::CarouselShadowStyle::None;
      } else {
        carouselStyles_[widgetSlot_] = HomeTheme::CarouselStyle::Centered;
        carouselLabels_[widgetSlot_] = 0;
        carouselLabelColors_[widgetSlot_] = HomeTheme::CarouselLabelColor::Black;
        carouselShadowStyles_[widgetSlot_] = HomeTheme::CarouselShadowStyle::None;
        }
        widgetPopup_ = false;
        render();
      }
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
      widgetSlot_ = std::max(0, widgetSlot_ - 1);
      render();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
      widgetSlot_ = std::min(HomeTheme::slotCount(layout_) - 1, widgetSlot_ + 1);
      render();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
      openWidgetPopup(widgetSlot_);
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
      openWidgetPopup(widgetSlot_);
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      saveEditorAndClose();
    }
  }
}
