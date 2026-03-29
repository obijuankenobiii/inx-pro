#include "HomeTheme.h"

#include <SDCardManager.h>
#include <Serialization.h>

#include <algorithm>
#include <cstring>
#include <string>

namespace HomeTheme {
namespace {

constexpr char kThemeFile[] = "/.system/home_themes.bin";
constexpr uint8_t kVersion = 10;
constexpr uint8_t kLegacyCarouselShadowVersion = 8;
constexpr uint8_t kLegacyCarouselShadowStyleVersion = 9;
constexpr uint8_t kSleepThemeVersion = 3;
constexpr int kMaxThemes = 8;

Theme themes[kMaxThemes] = {};
Theme sleepTheme = {};
int themeCount = 0;
int selectedTheme = 0;
bool loaded = false;

void setName(Theme& theme, const std::string& name) {
  std::strncpy(theme.name, name.c_str(), sizeof(theme.name) - 1);
  theme.name[sizeof(theme.name) - 1] = '\0';
}

bool defaultBackground(Widget widget);
CarouselStyle defaultCarouselStyleForWidget(Widget widget);
void setDefaultBackgrounds(Theme& theme);
void setDefaultCarouselStyles(Theme& theme);
bool defaultCarouselLabel(Widget widget);
void setDefaultCarouselLabels(Theme& theme);
CarouselLabelColor defaultCarouselLabelColor(Widget widget);
void setDefaultCarouselLabelColors(Theme& theme);
void setDefaultCarouselShadowStyles(Theme& theme);

void makeDefault() {
  themeCount = 1;
  selectedTheme = 0;
  themes[0] = {};
  setName(themes[0], "Home");
  themes[0].layout = Layout::OneByTwo;
  for (Widget& widget : themes[0].widgets) widget = Widget::Empty;
  themes[0].widgets[0] = Widget::TodaysReading;
  themes[0].widgets[1] = Widget::Carousel;
  setDefaultBackgrounds(themes[0]);
  setDefaultCarouselStyles(themes[0]);
  setDefaultCarouselLabels(themes[0]);
  setDefaultCarouselLabelColors(themes[0]);
  setDefaultCarouselShadowStyles(themes[0]);
  sleepTheme = {};
  setName(sleepTheme, "Sleep");
  sleepTheme.layout = Layout::OneByTwo;
  sleepTheme.widgets[0] = Widget::Clock;
}

bool repairEmptyHomeTheme() {
  if (themeCount <= 0 || themes[0].layout != Layout::OneByTwo ||
      themes[0].widgets[0] != Widget::Empty || themes[0].widgets[1] != Widget::Empty) {
    return false;
  }
  themes[0].widgets[0] = Widget::TodaysReading;
  themes[0].widgets[1] = Widget::Carousel;
  setDefaultBackgrounds(themes[0]);
  setDefaultCarouselStyles(themes[0]);
  setDefaultCarouselLabels(themes[0]);
  setDefaultCarouselLabelColors(themes[0]);
  setDefaultCarouselShadowStyles(themes[0]);
  return true;
}

void ensureLoaded() {
  if (!loaded) load();
}

bool validLayout(const uint8_t value) {
  return value <= static_cast<uint8_t>(Layout::TwoByTwo);
}

bool validWidget(const uint8_t value) {
  return value <= static_cast<uint8_t>(Widget::Favorites);
}

bool validBorder(const uint8_t value) {
  return value <= static_cast<uint8_t>(Border::Thick);
}

bool validCarouselStyle(const uint8_t value) {
  return value <= static_cast<uint8_t>(CarouselStyle::Right);
}

bool validCarouselShadowStyle(const uint8_t value) {
  return value <= static_cast<uint8_t>(CarouselShadowStyle::Gray);
}

bool defaultBackground(const Widget widget) {
  return widget == Widget::Carousel || widget == Widget::Recent;
}

bool defaultCarouselLabel(const Widget widget) {
  return widget == Widget::Carousel || widget == Widget::Favorites;
}

CarouselStyle defaultCarouselStyleForWidget(const Widget widget) {
  return widget == Widget::Favorites || widget == Widget::Recent ? CarouselStyle::Left : CarouselStyle::Centered;
}

void setDefaultBackgrounds(Theme& theme) {
  for (int i = 0; i < 4; ++i) theme.backgrounds[i] = defaultBackground(theme.widgets[i]) ? 1 : 0;
}

void setDefaultCarouselStyles(Theme& theme) {
  for (int i = 0; i < 4; ++i) theme.carouselStyles[i] = defaultCarouselStyleForWidget(theme.widgets[i]);
}

void setDefaultCarouselLabels(Theme& theme) {
  for (int i = 0; i < 4; ++i) theme.carouselLabels[i] = defaultCarouselLabel(theme.widgets[i]) ? 1 : 0;
}

CarouselLabelColor defaultCarouselLabelColor(const Widget /*widget*/) { return CarouselLabelColor::Black; }

void setDefaultCarouselLabelColors(Theme& theme) {
  for (int i = 0; i < 4; ++i) theme.carouselLabelColors[i] = defaultCarouselLabelColor(theme.widgets[i]);
}

void setDefaultCarouselShadowStyles(Theme& theme) {
  for (CarouselShadowStyle& style : theme.carouselShadowStyles) style = CarouselShadowStyle::None;
}

}  // namespace

void load() {
  loaded = true;
  makeDefault();

  FsFile file;
  if (!SdMan.openFileForRead("CPS", kThemeFile, file)) return;

  uint8_t version = 0;
  uint8_t storedCount = 0;
  uint8_t storedActive = 0;
  serialization::readPod(file, version);
  serialization::readPod(file, storedCount);
  serialization::readPod(file, storedActive);
  if ((version != 1 && version != 2 && version != 3 && version != 4 && version != 5 && version != 6 && version != 7 &&
       version != kLegacyCarouselShadowVersion && version != kLegacyCarouselShadowStyleVersion &&
       version != kVersion) || storedCount == 0 ||
      storedCount > kMaxThemes) {
    file.close();
    return;
  }

  themeCount = 0;
  auto readTheme = [&](Theme& theme) -> bool {
    std::string name;
    uint8_t layout = 0;
    serialization::readString(file, name);
    serialization::readPod(file, layout);
    if (!validLayout(layout) || name.empty()) return false;

    theme = {};
    setName(theme, name);
    theme.layout = static_cast<Layout>(layout);
    for (Widget& widget : theme.widgets) {
      uint8_t value = 0;
      serialization::readPod(file, value);
      if (!validWidget(value)) {
        return false;
      }
      widget = static_cast<Widget>(value);
    }
    for (Border& border : theme.borders) {
      if (version == 1) {
        border = Border::None;
        continue;
      }
      uint8_t value = 0;
      serialization::readPod(file, value);
      if (!validBorder(value)) {
        return false;
      }
      border = static_cast<Border>(value);
    }
    for (int i = 0; i < 4; ++i) {
      if (version < kLegacyCarouselShadowVersion) {
        theme.backgrounds[i] = defaultBackground(theme.widgets[i]) ? 1 : 0;
        continue;
      }
      serialization::readPod(file, theme.backgrounds[i]);
      theme.backgrounds[i] = theme.backgrounds[i] != 0 ? 1 : 0;
    }
    for (int i = 0; i < 4; ++i) {
      if (version < kLegacyCarouselShadowVersion) {
        theme.carouselStyles[i] = defaultCarouselStyleForWidget(theme.widgets[i]);
        continue;
      }
      uint8_t value = 0;
      serialization::readPod(file, value);
      if (!validCarouselStyle(value)) return false;
      theme.carouselStyles[i] = static_cast<CarouselStyle>(value);
    }
    for (int i = 0; i < 4; ++i) {
      if (version < kLegacyCarouselShadowVersion) {
        theme.carouselLabels[i] = defaultCarouselLabel(theme.widgets[i]) ? 1 : 0;
        continue;
      }
      serialization::readPod(file, theme.carouselLabels[i]);
      theme.carouselLabels[i] = theme.carouselLabels[i] != 0 ? 1 : 0;
    }
    for (int i = 0; i < 4; ++i) {
      if (version < kLegacyCarouselShadowVersion) {
        theme.carouselLabelColors[i] = defaultCarouselLabelColor(theme.widgets[i]);
        continue;
      }
      uint8_t value = 0;
      serialization::readPod(file, value);
      theme.carouselLabelColors[i] = value <= static_cast<uint8_t>(CarouselLabelColor::Gray)
                                         ? static_cast<CarouselLabelColor>(value)
                                         : defaultCarouselLabelColor(theme.widgets[i]);
    }
    uint8_t legacyShadows[4] = {};
    if (version >= kLegacyCarouselShadowVersion && version < kVersion) {
      for (uint8_t& shadow : legacyShadows) serialization::readPod(file, shadow);
    }
    for (int i = 0; i < 4; ++i) {
      if (version < kLegacyCarouselShadowStyleVersion) {
        theme.carouselShadowStyles[i] = legacyShadows[i] != 0 ? CarouselShadowStyle::Black
                                                                : CarouselShadowStyle::None;
        continue;
      }
      uint8_t value = 0;
      serialization::readPod(file, value);
      CarouselShadowStyle style = CarouselShadowStyle::None;
      if (version == kLegacyCarouselShadowStyleVersion) {
        // Version 9 used a separate enabled toggle and four style values.
        // Migrate those values into the new three-state style.
        if (legacyShadows[i] != 0) {
          style = value <= 1 ? CarouselShadowStyle::Black : CarouselShadowStyle::Gray;
        }
      } else if (validCarouselShadowStyle(value)) {
        style = static_cast<CarouselShadowStyle>(value);
      }
      theme.carouselShadowStyles[i] = style;
    }
    if (theme.layout == Layout::Classic) {
      theme.layout = Layout::OneByTwo;
      for (Widget& widget : theme.widgets) widget = Widget::Empty;
      for (Border& border : theme.borders) border = Border::None;
      for (uint8_t& background : theme.backgrounds) background = 0;
      for (CarouselStyle& style : theme.carouselStyles) style = CarouselStyle::Centered;
      for (uint8_t& label : theme.carouselLabels) label = 0;
      for (CarouselLabelColor& color : theme.carouselLabelColors) color = CarouselLabelColor::Black;
      for (CarouselShadowStyle& style : theme.carouselShadowStyles) style = CarouselShadowStyle::None;
    }
    return true;
  };

  for (uint8_t i = 0; i < storedCount; ++i) {
    if (!readTheme(themes[themeCount])) {
      file.close();
      makeDefault();
      return;
    }
    ++themeCount;
  }
  if (version >= kSleepThemeVersion && !readTheme(sleepTheme)) {
    file.close();
    makeDefault();
    return;
  }
  file.close();

  if (themeCount == 0) {
    makeDefault();
  } else {
    // The first entry is the permanent home theme. Keep its user-facing name
    // stable even when loading older theme files that called it Default or Classic.
    setName(themes[0], "Home");
    selectedTheme = std::min<int>(storedActive, themeCount - 1);
    if (version < kLegacyCarouselShadowVersion) {
      sleepTheme = {};
      setName(sleepTheme, "Sleep");
      sleepTheme.layout = Layout::OneByTwo;
      sleepTheme.widgets[0] = Widget::Clock;
    }
    if (repairEmptyHomeTheme()) {
      save();
    }
  }
}

bool save() {
  ensureLoaded();
  SdMan.mkdir("/.system");

  FsFile file;
  if (!SdMan.openFileForWrite("CPS", kThemeFile, file)) return false;
  serialization::writePod(file, kVersion);
  const uint8_t storedCount = static_cast<uint8_t>(themeCount);
  const uint8_t storedActive = static_cast<uint8_t>(selectedTheme);
  serialization::writePod(file, storedCount);
  serialization::writePod(file, storedActive);
  auto writeTheme = [&](const Theme& theme) {
    serialization::writeString(file, theme.name);
    const uint8_t layout = static_cast<uint8_t>(theme.layout);
    serialization::writePod(file, layout);
    for (const Widget widget : theme.widgets) {
      const uint8_t value = static_cast<uint8_t>(widget);
      serialization::writePod(file, value);
    }
    for (const Border border : theme.borders) {
      const uint8_t value = static_cast<uint8_t>(border);
      serialization::writePod(file, value);
    }
    for (const uint8_t background : theme.backgrounds) serialization::writePod(file, background);
    for (const CarouselStyle style : theme.carouselStyles) {
      const uint8_t value = static_cast<uint8_t>(style);
      serialization::writePod(file, value);
    }
    for (const uint8_t label : theme.carouselLabels) serialization::writePod(file, label);
    for (const CarouselLabelColor color : theme.carouselLabelColors) {
      serialization::writePod(file, static_cast<uint8_t>(color));
    }
    for (const CarouselShadowStyle style : theme.carouselShadowStyles) {
      serialization::writePod(file, static_cast<uint8_t>(style));
    }
  };
  for (int i = 0; i < themeCount; ++i) writeTheme(themes[i]);
  writeTheme(sleepTheme);
  file.close();
  return true;
}

int count() {
  ensureLoaded();
  return themeCount;
}

const Theme& at(const int index) {
  ensureLoaded();
  const int safeIndex = std::max(0, std::min(index, themeCount - 1));
  return themes[safeIndex];
}

const Theme& sleep() {
  ensureLoaded();
  return sleepTheme;
}

int activeIndex() {
  ensureLoaded();
  return selectedTheme;
}

const Theme& active() { return at(activeIndex()); }

void activate(const int index) {
  ensureLoaded();
  if (index < 0 || index >= themeCount) return;
  selectedTheme = index;
  save();
}

int add(const Layout layout, const Widget* widgets, const Border* borders, const uint8_t* backgrounds,
        const CarouselStyle* carouselStyles, const uint8_t* carouselLabels,
        const CarouselLabelColor* carouselLabelColors, const CarouselShadowStyle* carouselShadowStyles,
        const int slotCountValue) {
  ensureLoaded();
  if (themeCount >= kMaxThemes) return -1;

  Theme& theme = themes[themeCount];
  theme = {};
  setName(theme, "Theme " + std::to_string(themeCount));
  theme.layout = layout;
  for (int i = 0; i < 4; ++i) {
    theme.widgets[i] = layout != Layout::Classic && widgets && i < slotCountValue ? widgets[i] : Widget::Empty;
    theme.borders[i] = layout != Layout::Classic && borders && i < slotCountValue ? borders[i] : Border::None;
    theme.backgrounds[i] = layout != Layout::Classic && backgrounds && i < slotCountValue
                               ? (backgrounds[i] != 0 ? 1 : 0)
                               : (layout != Layout::Classic && defaultBackground(theme.widgets[i]) ? 1 : 0);
    theme.carouselStyles[i] = layout != Layout::Classic && carouselStyles && i < slotCountValue
                                  ? (validCarouselStyle(static_cast<uint8_t>(carouselStyles[i]))
                                         ? carouselStyles[i]
                                         : defaultCarouselStyleForWidget(theme.widgets[i]))
                                         : defaultCarouselStyleForWidget(theme.widgets[i]);
    theme.carouselLabels[i] = layout != Layout::Classic && carouselLabels && i < slotCountValue
                                  ? (carouselLabels[i] != 0 ? 1 : 0)
                                  : (layout != Layout::Classic && defaultCarouselLabel(theme.widgets[i]) ? 1 : 0);
    theme.carouselLabelColors[i] = layout != Layout::Classic && carouselLabelColors && i < slotCountValue
                                      ? carouselLabelColors[i]
                                      : defaultCarouselLabelColor(theme.widgets[i]);
    theme.carouselShadowStyles[i] = layout != Layout::Classic && carouselShadowStyles && i < slotCountValue &&
                                            validCarouselShadowStyle(static_cast<uint8_t>(carouselShadowStyles[i]))
                                        ? carouselShadowStyles[i]
                                        : CarouselShadowStyle::None;
  }
  ++themeCount;
  selectedTheme = themeCount - 1;
  save();
  return selectedTheme;
}

void update(const int index, const Layout layout, const Widget* widgets, const Border* borders,
            const uint8_t* backgrounds, const CarouselStyle* carouselStyles, const uint8_t* carouselLabels,
            const CarouselLabelColor* carouselLabelColors, const CarouselShadowStyle* carouselShadowStyles,
            const int slotCountValue) {
  ensureLoaded();
  if (index < 0 || index >= themeCount) return;
  Theme& theme = themes[index];
  theme.layout = layout;
  for (int i = 0; i < 4; ++i) {
    theme.widgets[i] = layout != Layout::Classic && widgets && i < slotCountValue ? widgets[i] : Widget::Empty;
    theme.borders[i] = layout != Layout::Classic && borders && i < slotCountValue ? borders[i] : Border::None;
    theme.backgrounds[i] = layout != Layout::Classic && backgrounds && i < slotCountValue
                               ? (backgrounds[i] != 0 ? 1 : 0)
                               : (layout != Layout::Classic && defaultBackground(theme.widgets[i]) ? 1 : 0);
    theme.carouselStyles[i] = layout != Layout::Classic && carouselStyles && i < slotCountValue
                                  ? (validCarouselStyle(static_cast<uint8_t>(carouselStyles[i]))
                                         ? carouselStyles[i]
                                         : defaultCarouselStyleForWidget(theme.widgets[i]))
                                         : defaultCarouselStyleForWidget(theme.widgets[i]);
    theme.carouselLabels[i] = layout != Layout::Classic && carouselLabels && i < slotCountValue
                                  ? (carouselLabels[i] != 0 ? 1 : 0)
                                  : (layout != Layout::Classic && defaultCarouselLabel(theme.widgets[i]) ? 1 : 0);
    theme.carouselLabelColors[i] = layout != Layout::Classic && carouselLabelColors && i < slotCountValue
                                      ? carouselLabelColors[i]
                                      : defaultCarouselLabelColor(theme.widgets[i]);
    theme.carouselShadowStyles[i] = layout != Layout::Classic && carouselShadowStyles && i < slotCountValue &&
                                            validCarouselShadowStyle(static_cast<uint8_t>(carouselShadowStyles[i]))
                                        ? carouselShadowStyles[i]
                                        : CarouselShadowStyle::None;
  }
  selectedTheme = index;
  save();
}

void updateSleep(const Layout layout, const Widget* widgets, const Border* borders, const uint8_t* backgrounds,
                 const CarouselStyle* carouselStyles, const uint8_t* carouselLabels,
                 const CarouselLabelColor* carouselLabelColors, const CarouselShadowStyle* carouselShadowStyles,
                 const int slotCountValue) {
  ensureLoaded();
  sleepTheme.layout = layout;
  for (int i = 0; i < 4; ++i) {
    sleepTheme.widgets[i] = layout != Layout::Classic && widgets && i < slotCountValue ? widgets[i] : Widget::Empty;
    sleepTheme.borders[i] = layout != Layout::Classic && borders && i < slotCountValue ? borders[i] : Border::None;
    sleepTheme.backgrounds[i] = layout != Layout::Classic && backgrounds && i < slotCountValue
                                    ? (backgrounds[i] != 0 ? 1 : 0)
                                    : (layout != Layout::Classic && defaultBackground(sleepTheme.widgets[i]) ? 1 : 0);
    sleepTheme.carouselStyles[i] = layout != Layout::Classic && carouselStyles && i < slotCountValue
                                       ? (validCarouselStyle(static_cast<uint8_t>(carouselStyles[i]))
                                              ? carouselStyles[i]
                                              : defaultCarouselStyleForWidget(sleepTheme.widgets[i]))
                                              : defaultCarouselStyleForWidget(sleepTheme.widgets[i]);
    sleepTheme.carouselLabels[i] = layout != Layout::Classic && carouselLabels && i < slotCountValue
                                       ? (carouselLabels[i] != 0 ? 1 : 0)
                                       : (layout != Layout::Classic && defaultCarouselLabel(sleepTheme.widgets[i]) ? 1 : 0);
    sleepTheme.carouselLabelColors[i] = layout != Layout::Classic && carouselLabelColors && i < slotCountValue
                                           ? carouselLabelColors[i]
                                           : defaultCarouselLabelColor(sleepTheme.widgets[i]);
    sleepTheme.carouselShadowStyles[i] = layout != Layout::Classic && carouselShadowStyles && i < slotCountValue &&
                                                 validCarouselShadowStyle(static_cast<uint8_t>(carouselShadowStyles[i]))
                                             ? carouselShadowStyles[i]
                                             : CarouselShadowStyle::None;
  }
  save();
}

bool remove(const int index) {
  ensureLoaded();
  if (index <= 0 || index >= themeCount || themeCount <= 1) return false;

  for (int i = index; i < themeCount - 1; ++i) themes[i] = themes[i + 1];
  --themeCount;
  if (selectedTheme > index) {
    --selectedTheme;
  } else if (selectedTheme == index) {
    selectedTheme = std::min(selectedTheme, themeCount - 1);
  }
  save();
  return true;
}

const char* layoutLabel(const Layout layout) {
  switch (layout) {
    case Layout::Classic:
      return "Home";
    case Layout::OneByTwo:
      return "1 x 2";
    case Layout::TwoByTwo:
      return "2 x 2";
    default:
      return "Unknown";
  }
}

const char* widgetLabel(const Widget widget) {
  switch (widget) {
    case Widget::Empty:
      return "Empty";
    case Widget::Carousel:
      return "Recent Carousel";
    case Widget::Shortcuts:
      return "Shortcuts";
    case Widget::Clock:
      return "Clock";
    case Widget::Recent:
      return "Recent";
    case Widget::Temperature:
      return "Temperature";
    case Widget::Calendar:
      return "Calendar";
    case Widget::ListShortcuts:
      return "List shortcuts";
    case Widget::Humidity:
      return "Humidity";
    case Widget::TodaysReading:
      return "Today's Reading";
    case Widget::Favorites:
      return "Favorite Carousel";
    default:
      return "Unknown";
  }
}

const char* carouselStyleLabel(const CarouselStyle style) {
  switch (style) {
    case CarouselStyle::Centered:
      return "Centered";
    case CarouselStyle::Left:
      return "Left";
    case CarouselStyle::Right:
      return "Right";
    default:
      return "Centered";
  }
}

const char* carouselLabelColorLabel(const CarouselLabelColor color) {
  switch (color) {
    case CarouselLabelColor::Black:
      return "Black";
    case CarouselLabelColor::Gray:
      return "Gray";
    default:
      return "Black";
  }
}

const char* carouselShadowStyleLabel(const CarouselShadowStyle style) {
  switch (style) {
    case CarouselShadowStyle::None:
      return "None";
    case CarouselShadowStyle::Black:
      return "Black";
    case CarouselShadowStyle::Gray:
      return "Gray";
    default:
      return "None";
  }
}

CarouselStyle defaultCarouselStyle(const Widget widget) { return defaultCarouselStyleForWidget(widget); }

int slotCount(const Layout layout) { return layout == Layout::TwoByTwo ? 4 : layout == Layout::OneByTwo ? 2 : 0; }

}  // namespace HomeTheme
