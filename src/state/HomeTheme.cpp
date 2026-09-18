#include "HomeTheme.h"

#include <SDCardManager.h>
#include <Serialization.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cstring>
#include <string>

namespace HomeTheme {
namespace {

constexpr char kThemeFile[] = "/.system/home_themes.bin";
constexpr uint8_t kVersion = 19;
constexpr uint8_t kDescriptionOptionsVersion = 17;
constexpr uint8_t kRecentOptionsVersion = 18;
constexpr uint8_t kLibraryFoldersVersion = 17;
constexpr uint8_t kLegacyLibraryFoldersVersion = 13;
constexpr uint8_t kLegacyMultiLibraryFoldersVersion = 14;
constexpr uint8_t kLegacySingleLibraryFoldersVersion = 15;
constexpr uint8_t kHeatmapViewVersion = 12;
constexpr uint8_t kLegacyCarouselShadowVersion = 8;
constexpr uint8_t kLegacyCarouselShadowStyleVersion = 9;
constexpr uint8_t kSleepThemeVersion = 3;
constexpr int kMaxThemes = 8;

// These tables are persistent settings, not hot rendering buffers. Allocate
// them once from PSRAM so the internal heap remains available to the reader
// and display paths while preserving the existing reference-based API.
Theme* themeStorage = nullptr;
Theme* themes = nullptr;
Theme* sleepThemeStorage = nullptr;
#define sleepTheme (*sleepThemeStorage)
int themeCount = 0;
int selectedTheme = 0;
bool loaded = false;

bool ensureStorage() {
  if (themeStorage != nullptr) return true;

  constexpr uint32_t caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
  themeStorage = static_cast<Theme*>(heap_caps_calloc(kMaxThemes + 1, sizeof(Theme), caps));
  if (themeStorage == nullptr) {
    // Keep the reader functional if PSRAM is unavailable, while still
    // preferring PSRAM on the target boards.
    themeStorage = static_cast<Theme*>(heap_caps_calloc(kMaxThemes + 1, sizeof(Theme), MALLOC_CAP_8BIT));
  }
  if (themeStorage == nullptr) return false;

  themes = themeStorage;
  sleepThemeStorage = themeStorage + kMaxThemes;
  return true;
}

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
void setDefaultDescriptionOptions(Theme& theme);
void setDefaultRecentOptions(Theme& theme);
void setDefaultCarouselProgress(Theme& theme);
void setDefaultHeatmapViews(Theme& theme);
void setDefaultLibraryFolders(Theme& theme);

void makeDefault() {
  themeCount = 1;
  selectedTheme = 0;
  themes[0] = {};
  setName(themes[0], "Home");
  themes[0].layout = Layout::OneByTwo;
  for (Widget& widget : themes[0].widgets) widget = Widget::Empty;
  themes[0].widgets[0] = Widget::Carousel;
  themes[0].widgets[1] = Widget::Description;
  setDefaultBackgrounds(themes[0]);
  setDefaultCarouselStyles(themes[0]);
  setDefaultCarouselLabels(themes[0]);
  setDefaultCarouselLabelColors(themes[0]);
  setDefaultCarouselShadowStyles(themes[0]);
  setDefaultDescriptionOptions(themes[0]);
  setDefaultRecentOptions(themes[0]);
  setDefaultCarouselProgress(themes[0]);
  setDefaultHeatmapViews(themes[0]);
  setDefaultLibraryFolders(themes[0]);
  themes[0].carouselStyles[0] = CarouselStyle::Left;
  themes[0].borders[0] = Border::Normal;
  themes[0].carouselLabels[0] = 0;
  themes[0].backgrounds[0] = 1;
  themes[0].carouselShadowStyles[0] = CarouselShadowStyle::Gray;
  themes[0].carouselProgress[0] = 1;
  themes[0].backgrounds[1] = 0;
  themes[0].descriptionProgress[1] = 0;
  sleepTheme = {};
  setName(sleepTheme, "Sleep");
  sleepTheme.layout = Layout::OneByTwo;
  sleepTheme.widgets[0] = Widget::Clock;
  setDefaultLibraryFolders(sleepTheme);
}

bool repairEmptyHomeTheme() {
  if (themeCount <= 0 || themes[0].layout != Layout::OneByTwo ||
      themes[0].widgets[0] != Widget::Empty || themes[0].widgets[1] != Widget::Empty) {
    return false;
  }
  themes[0].widgets[0] = Widget::Carousel;
  themes[0].widgets[1] = Widget::Description;
  setDefaultBackgrounds(themes[0]);
  setDefaultCarouselStyles(themes[0]);
  setDefaultCarouselLabels(themes[0]);
  setDefaultCarouselLabelColors(themes[0]);
  setDefaultCarouselShadowStyles(themes[0]);
  setDefaultDescriptionOptions(themes[0]);
  setDefaultRecentOptions(themes[0]);
  setDefaultCarouselProgress(themes[0]);
  setDefaultHeatmapViews(themes[0]);
  setDefaultLibraryFolders(themes[0]);
  themes[0].carouselStyles[0] = CarouselStyle::Left;
  themes[0].borders[0] = Border::Normal;
  themes[0].carouselLabels[0] = 0;
  themes[0].backgrounds[0] = 1;
  themes[0].carouselShadowStyles[0] = CarouselShadowStyle::Gray;
  themes[0].carouselProgress[0] = 1;
  themes[0].backgrounds[1] = 0;
  themes[0].descriptionProgress[1] = 0;
  return true;
}

void ensureLoaded() {
  if (!loaded) load();
}

bool validLayout(const uint8_t value) { return value <= static_cast<uint8_t>(Layout::TwoByTwo); }

bool validWidget(const uint8_t value) {
  return value <= static_cast<uint8_t>(Widget::Description);
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

bool validHeatmapView(const uint8_t value) { return value <= static_cast<uint8_t>(HeatmapView::Monthly); }

bool defaultBackground(const Widget widget) {
  return widget == Widget::Carousel || widget == Widget::Recent;
}

bool defaultCarouselLabel(const Widget widget) {
  return widget == Widget::Carousel || widget == Widget::Favorites || widget == Widget::Heatmap;
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

void setDefaultDescriptionOptions(Theme& theme) {
  for (int i = 0; i < 4; ++i) {
    theme.descriptionTitles[i] = 1;
    theme.descriptionAuthors[i] = 1;
    theme.descriptionProgress[i] = 1;
  }
}

void setDefaultRecentOptions(Theme& theme) {
  for (int i = 0; i < 4; ++i) {
    theme.recentTitles[i] = 1;
    theme.recentAuthors[i] = 1;
    theme.recentProgress[i] = 1;
  }
}

void setDefaultCarouselProgress(Theme& theme) {
  for (uint8_t& value : theme.carouselProgress) value = 1;
}

void setDefaultHeatmapViews(Theme& theme) {
  for (HeatmapView& view : theme.heatmapViews) view = HeatmapView::Weekly;
}

void setDefaultLibraryFolders(Theme& theme) {
  for (auto& library : theme.libraryFolders) {
    for (char (&folder)[128] : library) {
      std::strncpy(folder, "/", sizeof(folder) - 1);
      folder[sizeof(folder) - 1] = '\0';
    }
  }
}

void setLibraryFolder(char (&destination)[128], const char* source) {
  const std::string value = source && source[0] == '/' ? source : "/";
  std::strncpy(destination, value.c_str(), sizeof(destination) - 1);
  destination[sizeof(destination) - 1] = '\0';
}

}

void load() {
  if (!ensureStorage()) {
    loaded = true;
    themeCount = 0;
    selectedTheme = 0;
    return;
  }
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
       version != 10 && version != 11 && version != kHeatmapViewVersion && version != kLegacyLibraryFoldersVersion &&
       version != kLegacyMultiLibraryFoldersVersion && version != kLegacySingleLibraryFoldersVersion && version != 16 &&
       version != 17 && version != 18 &&
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
    if ((!validLayout(layout) && layout != 3) || name.empty()) return false;

    theme = {};
    setName(theme, name);
    theme.layout = layout == 3 ? Layout::OneByTwo : static_cast<Layout>(layout);
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
      if (version < kDescriptionOptionsVersion && theme.widgets[i] == Widget::Heatmap) theme.carouselLabels[i] = 1;
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
    if (version == kLegacyCarouselShadowVersion || version == kLegacyCarouselShadowStyleVersion) {
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
        if (legacyShadows[i] != 0) {
          style = value <= 1 ? CarouselShadowStyle::Black : CarouselShadowStyle::Gray;
        }
      } else if (validCarouselShadowStyle(value)) {
        style = static_cast<CarouselShadowStyle>(value);
      }
      theme.carouselShadowStyles[i] = style;
    }
    for (int i = 0; i < 4; ++i) {
      if (version < kDescriptionOptionsVersion) {
        theme.descriptionTitles[i] = 1;
        theme.descriptionAuthors[i] = 1;
        theme.descriptionProgress[i] = 1;
        continue;
      }
      serialization::readPod(file, theme.descriptionTitles[i]);
      serialization::readPod(file, theme.descriptionAuthors[i]);
      serialization::readPod(file, theme.descriptionProgress[i]);
      theme.descriptionTitles[i] = theme.descriptionTitles[i] != 0 ? 1 : 0;
      theme.descriptionAuthors[i] = theme.descriptionAuthors[i] != 0 ? 1 : 0;
      theme.descriptionProgress[i] = theme.descriptionProgress[i] != 0 ? 1 : 0;
    }
    for (int i = 0; i < 4; ++i) {
      if (version < kRecentOptionsVersion) {
        theme.recentTitles[i] = 1;
        theme.recentAuthors[i] = 1;
        theme.recentProgress[i] = 1;
        continue;
      }
      serialization::readPod(file, theme.recentTitles[i]);
      serialization::readPod(file, theme.recentAuthors[i]);
      serialization::readPod(file, theme.recentProgress[i]);
      theme.recentTitles[i] = theme.recentTitles[i] != 0 ? 1 : 0;
      theme.recentAuthors[i] = theme.recentAuthors[i] != 0 ? 1 : 0;
      theme.recentProgress[i] = theme.recentProgress[i] != 0 ? 1 : 0;
    }
    for (int i = 0; i < 4; ++i) {
      if (version < kVersion) {
        theme.carouselProgress[i] = 1;
        continue;
      }
      serialization::readPod(file, theme.carouselProgress[i]);
      theme.carouselProgress[i] = theme.carouselProgress[i] != 0 ? 1 : 0;
    }
    for (int i = 0; i < 4; ++i) {
      if (version < kHeatmapViewVersion) {
        theme.heatmapViews[i] = HeatmapView::Weekly;
        continue;
      }
      uint8_t value = 0;
      serialization::readPod(file, value);
      theme.heatmapViews[i] = validHeatmapView(value) ? static_cast<HeatmapView>(value) : HeatmapView::Weekly;
    }
    if (version >= kLibraryFoldersVersion || version == kLegacyMultiLibraryFoldersVersion) {
      for (auto& library : theme.libraryFolders) {
        std::string value;
        serialization::readString(file, value);
        setLibraryFolder(library[0], value.c_str());
        if (version >= kLibraryFoldersVersion || version == kLegacyMultiLibraryFoldersVersion) {
          for (int extra = 1; extra < 3; ++extra) {
            if (version >= kLibraryFoldersVersion || version == kLegacyMultiLibraryFoldersVersion) {
              serialization::readString(file, value);
              setLibraryFolder(library[extra], value.c_str());
            } else {
              setLibraryFolder(library[extra], "/");
            }
          }
        }
      }
    } else if (version == kLegacyLibraryFoldersVersion || version == kLegacySingleLibraryFoldersVersion) {
      for (auto& library : theme.libraryFolders) {
        std::string value;
        serialization::readString(file, value);
        setLibraryFolder(library[0], value.c_str());
        setLibraryFolder(library[1], "/");
        setLibraryFolder(library[2], "/");
      }
    } else {
      setDefaultLibraryFolders(theme);
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
      setDefaultDescriptionOptions(theme);
      setDefaultRecentOptions(theme);
      setDefaultCarouselProgress(theme);
      for (HeatmapView& view : theme.heatmapViews) view = HeatmapView::Weekly;
      setDefaultLibraryFolders(theme);
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
    for (int i = 0; i < 4; ++i) {
      serialization::writePod(file, theme.descriptionTitles[i]);
      serialization::writePod(file, theme.descriptionAuthors[i]);
      serialization::writePod(file, theme.descriptionProgress[i]);
    }
    for (int i = 0; i < 4; ++i) {
      serialization::writePod(file, theme.recentTitles[i]);
      serialization::writePod(file, theme.recentAuthors[i]);
      serialization::writePod(file, theme.recentProgress[i]);
    }
    for (int i = 0; i < 4; ++i) serialization::writePod(file, theme.carouselProgress[i]);
    for (const HeatmapView view : theme.heatmapViews) {
      serialization::writePod(file, static_cast<uint8_t>(view));
    }
    for (const auto& library : theme.libraryFolders) {
      for (const char (&folder)[128] : library) serialization::writeString(file, std::string(folder));
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
        const HeatmapView* heatmapViews, const uint8_t* descriptionTitles, const uint8_t* descriptionAuthors,
        const uint8_t* descriptionProgress, const uint8_t* recentTitles, const uint8_t* recentAuthors,
        const uint8_t* recentProgress, const uint8_t* carouselProgress,
        const char (*libraryFolders)[3][128],
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
    theme.descriptionTitles[i] = layout != Layout::Classic && descriptionTitles && i < slotCountValue
                                     ? (descriptionTitles[i] != 0 ? 1 : 0)
                                     : 1;
    theme.descriptionAuthors[i] = layout != Layout::Classic && descriptionAuthors && i < slotCountValue
                                      ? (descriptionAuthors[i] != 0 ? 1 : 0)
                                      : 1;
    theme.descriptionProgress[i] = layout != Layout::Classic && descriptionProgress && i < slotCountValue
                                       ? (descriptionProgress[i] != 0 ? 1 : 0)
                                       : 1;
    theme.recentTitles[i] = layout != Layout::Classic && recentTitles && i < slotCountValue
                                ? (recentTitles[i] != 0 ? 1 : 0)
                                : 1;
    theme.recentAuthors[i] = layout != Layout::Classic && recentAuthors && i < slotCountValue
                                 ? (recentAuthors[i] != 0 ? 1 : 0)
                                 : 1;
    theme.recentProgress[i] = layout != Layout::Classic && recentProgress && i < slotCountValue
                                  ? (recentProgress[i] != 0 ? 1 : 0)
                                  : 1;
    theme.carouselProgress[i] = layout != Layout::Classic && carouselProgress && i < slotCountValue
                                    ? (carouselProgress[i] != 0 ? 1 : 0)
                                    : 1;
    theme.heatmapViews[i] = layout != Layout::Classic && heatmapViews && i < slotCountValue &&
                                   validHeatmapView(static_cast<uint8_t>(heatmapViews[i]))
                               ? heatmapViews[i]
                               : HeatmapView::Weekly;
    for (int folder = 0; folder < 3; ++folder) {
      setLibraryFolder(theme.libraryFolders[i][folder], layout != Layout::Classic && libraryFolders && i < slotCountValue
                                                         ? libraryFolders[i][folder]
                                                         : "/");
    }
  }
  ++themeCount;
  selectedTheme = themeCount - 1;
  save();
  return selectedTheme;
}

void update(const int index, const Layout layout, const Widget* widgets, const Border* borders,
            const uint8_t* backgrounds, const CarouselStyle* carouselStyles, const uint8_t* carouselLabels,
            const CarouselLabelColor* carouselLabelColors, const CarouselShadowStyle* carouselShadowStyles,
            const HeatmapView* heatmapViews, const uint8_t* descriptionTitles, const uint8_t* descriptionAuthors,
            const uint8_t* descriptionProgress, const uint8_t* recentTitles, const uint8_t* recentAuthors,
            const uint8_t* recentProgress, const uint8_t* carouselProgress,
            const char (*libraryFolders)[3][128],
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
    theme.descriptionTitles[i] = layout != Layout::Classic && descriptionTitles && i < slotCountValue
                                     ? (descriptionTitles[i] != 0 ? 1 : 0)
                                     : 1;
    theme.descriptionAuthors[i] = layout != Layout::Classic && descriptionAuthors && i < slotCountValue
                                      ? (descriptionAuthors[i] != 0 ? 1 : 0)
                                      : 1;
    theme.descriptionProgress[i] = layout != Layout::Classic && descriptionProgress && i < slotCountValue
                                       ? (descriptionProgress[i] != 0 ? 1 : 0)
                                       : 1;
    theme.recentTitles[i] = layout != Layout::Classic && recentTitles && i < slotCountValue
                                ? (recentTitles[i] != 0 ? 1 : 0)
                                : 1;
    theme.recentAuthors[i] = layout != Layout::Classic && recentAuthors && i < slotCountValue
                                 ? (recentAuthors[i] != 0 ? 1 : 0)
                                 : 1;
    theme.recentProgress[i] = layout != Layout::Classic && recentProgress && i < slotCountValue
                                  ? (recentProgress[i] != 0 ? 1 : 0)
                                  : 1;
    theme.carouselProgress[i] = layout != Layout::Classic && carouselProgress && i < slotCountValue
                                    ? (carouselProgress[i] != 0 ? 1 : 0)
                                    : 1;
    theme.heatmapViews[i] = layout != Layout::Classic && heatmapViews && i < slotCountValue &&
                                   validHeatmapView(static_cast<uint8_t>(heatmapViews[i]))
                               ? heatmapViews[i]
                               : HeatmapView::Weekly;
    for (int folder = 0; folder < 3; ++folder) {
      setLibraryFolder(theme.libraryFolders[i][folder], layout != Layout::Classic && libraryFolders && i < slotCountValue
                                                         ? libraryFolders[i][folder]
                                                         : "/");
    }
  }
  selectedTheme = index;
  save();
}

void updateSleep(const Layout layout, const Widget* widgets, const Border* borders, const uint8_t* backgrounds,
                 const CarouselStyle* carouselStyles, const uint8_t* carouselLabels,
                 const CarouselLabelColor* carouselLabelColors, const CarouselShadowStyle* carouselShadowStyles,
                 const HeatmapView* heatmapViews, const uint8_t* descriptionTitles, const uint8_t* descriptionAuthors,
                 const uint8_t* descriptionProgress, const uint8_t* recentTitles, const uint8_t* recentAuthors,
                 const uint8_t* recentProgress, const uint8_t* carouselProgress,
                 const char (*libraryFolders)[3][128],
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
    sleepTheme.descriptionTitles[i] = layout != Layout::Classic && descriptionTitles && i < slotCountValue
                                          ? (descriptionTitles[i] != 0 ? 1 : 0)
                                          : 1;
    sleepTheme.descriptionAuthors[i] = layout != Layout::Classic && descriptionAuthors && i < slotCountValue
                                           ? (descriptionAuthors[i] != 0 ? 1 : 0)
                                           : 1;
    sleepTheme.descriptionProgress[i] = layout != Layout::Classic && descriptionProgress && i < slotCountValue
                                            ? (descriptionProgress[i] != 0 ? 1 : 0)
                                            : 1;
    sleepTheme.recentTitles[i] = layout != Layout::Classic && recentTitles && i < slotCountValue
                                     ? (recentTitles[i] != 0 ? 1 : 0)
                                     : 1;
    sleepTheme.recentAuthors[i] = layout != Layout::Classic && recentAuthors && i < slotCountValue
                                      ? (recentAuthors[i] != 0 ? 1 : 0)
                                      : 1;
    sleepTheme.recentProgress[i] = layout != Layout::Classic && recentProgress && i < slotCountValue
                                       ? (recentProgress[i] != 0 ? 1 : 0)
                                       : 1;
    sleepTheme.carouselProgress[i] = layout != Layout::Classic && carouselProgress && i < slotCountValue
                                         ? (carouselProgress[i] != 0 ? 1 : 0)
                                         : 1;
    sleepTheme.heatmapViews[i] = layout != Layout::Classic && heatmapViews && i < slotCountValue &&
                                         validHeatmapView(static_cast<uint8_t>(heatmapViews[i]))
                                     ? heatmapViews[i]
                                     : HeatmapView::Weekly;
    for (int folder = 0; folder < 3; ++folder) {
      setLibraryFolder(sleepTheme.libraryFolders[i][folder],
                       layout != Layout::Classic && libraryFolders && i < slotCountValue
                           ? libraryFolders[i][folder]
                           : "/");
    }
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
    case Widget::Heatmap:
      return "Reading Heatmap";
    case Widget::Library:
      return "Library";
    case Widget::Description:
      return "Description";
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

const char* heatmapViewLabel(const HeatmapView view) {
  switch (view) {
    case HeatmapView::Daily:
      return "Daily";
    case HeatmapView::Weekly:
      return "Weekly";
    case HeatmapView::Monthly:
      return "Monthly";
    default:
      return "Weekly";
  }
}

int slotCount(const Layout layout) {
  return layout == Layout::TwoByTwo ? 4 : layout == Layout::OneByTwo ? 2 : 0;
}

}
