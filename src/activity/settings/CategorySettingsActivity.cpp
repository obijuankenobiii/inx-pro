/**
 * @file CategorySettingsActivity.cpp
 * @brief Definitions for CategorySettingsActivity.
 */

#include "CategorySettingsActivity.h"
#include "system/UiLayout.h"
#include "activity/page/Page.h"

#include <EpdFontFamily.h>
#include <GfxRenderer.h>
#include <HardwareSerial.h>
#include <SDCardManager.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "system/TouchHitTest.h"
#include <iterator>
#include <string>

#include "CalibreSettingsActivity.h"
#include "AuthorGeneratorActivity.h"
#include "ClearCacheActivity.h"
#include "ClockStylePickerActivity.h"
#include "TimeSyncActivity.h"
#include "activity/page/components/global/PopUp.h"
#include "ReaderFontSettingsDraw.h"
#include "SleepImagePickerActivity.h"
#include "ThumbnailGeneratorActivity.h"
#include "ThemePickerActivity.h"
#include "images/Close.h"
#include "images/LibraryFilterLeft.h"
#include "images/LibraryFilterRight.h"
#include "state/ReaderSetting.h"
#include "state/SystemSetting.h"
#include "system/FontManager.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "util/StringUtils.h"

namespace {
constexpr const char* kSleepImageIndexPath = "/.system/sleep_images.idx";
constexpr uint8_t kSelectorModeSetting = 0;
constexpr uint8_t kSelectorModeSleepImage = 1;
constexpr const char* kSleepImageRefreshValue = "__refresh";

struct SettingsTabLayout {
  int systemX;
  int dividerX;
  int readerX;
  int systemW;
  int readerW;
};

SettingsTabLayout settingsTabLayout(const GfxRenderer& renderer) {
  const int font = systemFontId();
  const int systemW = renderer.text.getWidth(font, "System", EpdFontFamily::BOLD);
  const int readerW = renderer.text.getWidth(font, "Reader", EpdFontFamily::BOLD);
  const int dividerX = renderer.getScreenWidth() / 2;
  return {20, dividerX, dividerX + 20, systemW, readerW};
}

bool isSupportedSleepImageFile(const std::string& filename) {
  return StringUtils::checkFileExtension(filename, ".bmp") || StringUtils::checkFileExtension(filename, ".jpg") ||
         StringUtils::checkFileExtension(filename, ".jpeg");
}

void writeString(FsFile& file, const std::string& s) {
  file.write(reinterpret_cast<const uint8_t*>(s.c_str()), s.length());
}

void drawValueStepper(GfxRenderer& renderer, const char* value, int left, int right, int itemY, int itemHeight) {
  constexpr int iconSize = 30;
  constexpr int gap = 8;
  const int font = systemFontId();
  const int valueWidth = renderer.text.getWidth(font, value, EpdFontFamily::REGULAR);
  const int width = iconSize + gap + valueWidth + gap + iconSize;
  const int x = std::max(left, right - width);
  const int iconY = itemY + (itemHeight - iconSize) / 2;
  const int textY = itemY + (itemHeight - renderer.text.getLineHeight(font)) / 2;

  renderer.bitmap.icon(LibraryFilterLeft, x, iconY, iconSize, iconSize);
  renderer.text.render(font, x + iconSize + gap, textY, value, true, EpdFontFamily::REGULAR);
  renderer.bitmap.icon(LibraryFilterRight, x + iconSize + gap + valueWidth + gap, iconY, iconSize, iconSize);
}

void markManualTimezoneSelection(uint8_t SystemSetting::* valuePtr) {
  if (valuePtr == &SystemSetting::timeZoneQuarterOffset) {
    SETTINGS.timeZoneAutoDetectEnabled = 0;
    SETTINGS.timeZoneId[0] = '\0';
  }
}

}

/**
 * @brief Static trampoline function for task creation
 */
void CategorySettingsActivity::taskTrampoline(void* param) {
  auto* self = static_cast<CategorySettingsActivity*>(param);
  self->displayTaskLoop();
}

/**
 * @brief Initialize activity state and create display task
 */
void CategorySettingsActivity::onEnter() {
  Activity::onEnter();
  renderingMutex = xSemaphoreCreateMutex();

  halfRefreshOnLoadApplied_ = false;
  selectedIndex = -1;
  scrollOffset = 0;
  updateRequired = true;

  if (categoryName != nullptr && strcmp(categoryName, "Reader") == 0) {
    FontManager::scanSDFonts("/fonts", true);
    FontManager::clampReaderFontFamilySlot(READER_SETTINGS.fontFamily);
  }

  setupMenu();

  if (embedded) {
    render();
    updateRequired = false;
    return;
  }

  xTaskCreate(&CategorySettingsActivity::taskTrampoline, "CategorySettingsActivityTask", 4096, this, 1,
              &displayTaskHandle);
}

/**
 * @brief Clean up resources and delete display task
 */
void CategorySettingsActivity::onExit() {
  ActivityWithSubactivity::onExit();

  if (renderingMutex) {
    xSemaphoreTake(renderingMutex, portMAX_DELAY);
  }

  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }

  if (renderingMutex) {
    xSemaphoreGive(renderingMutex);
    vSemaphoreDelete(renderingMutex);
    renderingMutex = nullptr;
  }
}

void CategorySettingsActivity::navigateToSelectedMenu() {
  if (tabSelectorIndex == 0 && onTabRecent) {
    onTabRecent();
    return;
  }
  if (tabSelectorIndex == 1 && onTabLibrary) {
    onTabLibrary();
    return;
  }
  if (tabSelectorIndex == 3 && onTabSync) {
    onTabSync();
    return;
  }
  if (tabSelectorIndex == 4 && onTabStatistics) {
    onTabStatistics();
    return;
  }
}

/**
 * @brief Toggles expansion state of a group
 */
void CategorySettingsActivity::toggleGroup(GroupType group) {
  if (group == GroupType::THEME) {
    exitActivity();
    enterNewActivity(new ThemePickerActivity(renderer, mappedInput, [this] {
      exitActivity();
      updateRequired = true;
    }));
    return;
  }
  openGroup(group);
}

void CategorySettingsActivity::openGroup(const GroupType group) {
  detailGroup = group;
  groupOpen = true;
  detailScroll = 0;
  groupExpanded_.fill(false);
  groupExpanded_[groupIndex(group)] = true;
  setupMenu();
  selectedIndex = -1;
  updateRequired = true;
}

void CategorySettingsActivity::closeGroup() {
  groupOpen = false;
  detailGroup = GroupType::NONE;
  detailScroll = 0;
  groupExpanded_.fill(false);
  setupMenu();
  selectedIndex = -1;
  scrollOffset = 0;
  updateRequired = true;
}

void CategorySettingsActivity::detailRows(std::vector<int>& rows) const {
  rows.clear();
  for (int i = 0; i < static_cast<int>(menuItems.size()); ++i) {
    const MenuEntry& entry = menuItems[static_cast<size_t>(i)];
    if (entry.group == detailGroup && entry.type != SettingType::SEPARATOR) {
      rows.push_back(i);
    }
  }
}

bool CategorySettingsActivity::groupInput() {
  if (!groupOpen || selectorOpen || subActivity) return false;

  std::vector<int> rows;
  detailRows(rows);
  const int rowHeight = Page::LIST_ITEM_HEIGHT;
  const int listTop = navigation::Menu::height + 20;
  const int visible = std::max(1, (renderer.getScreenHeight() - listTop - 10) / rowHeight);
  const int maxScroll = std::max(0, static_cast<int>(rows.size()) - visible);

  if (mappedInput.hasTouch() && mappedInput.wasTouchSwipeUp()) {
    closeGroup();
    return true;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    closeGroup();
    return true;
  }

  if (mappedInput.hasTouch()) {
    float tapNx = 0.0f;
    float tapNy = 0.0f;
    if (mappedInput.wasTouchTapInScreen(renderer, tapNx, tapNy)) {
      const int tapX = static_cast<int>(tapNx * renderer.getScreenWidth());
      const int tapY = static_cast<int>(tapNy * renderer.getScreenHeight());
      if (tapX >= renderer.getScreenWidth() - 60 && tapX < renderer.getScreenWidth() - 20 && tapY >= 20 &&
          tapY < 60) {
        closeGroup();
        return true;
      }
      if (tapY >= listTop && tapY < listTop + visible * rowHeight) {
        const int row = detailScroll + (tapY - listTop) / rowHeight;
        if (row >= 0 && row < static_cast<int>(rows.size())) {
          selectedIndex = rows[static_cast<size_t>(row)];
          const MenuEntry& selected = menuItems[static_cast<size_t>(selectedIndex)];
          if (selected.type == SettingType::TOGGLE) {
            selected.change(0);
            selectedIndex = -1;
          } else if (selected.type == SettingType::ENUM || selected.type == SettingType::VALUE) {
            openSelectorForSelected();
            selectedIndex = -1;
          } else if (selected.type == SettingType::ACTION) {
            selected.change(0);
            selectedIndex = -1;
          }
          return true;
        }
      }
      closeGroup();
      return true;
    }
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    int position = -1;
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
      if (rows[static_cast<size_t>(i)] == selectedIndex) position = i;
    }
    position = position <= 0 ? static_cast<int>(rows.size()) - 1 : position - 1;
    if (!rows.empty()) {
      selectedIndex = rows[static_cast<size_t>(position)];
      detailScroll = std::max(0, std::min(maxScroll, position - visible + 1));
      updateRequired = true;
    }
    return true;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    int position = -1;
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
      if (rows[static_cast<size_t>(i)] == selectedIndex) position = i;
    }
    position = position < 0 || position + 1 >= static_cast<int>(rows.size()) ? 0 : position + 1;
    if (!rows.empty()) {
      selectedIndex = rows[static_cast<size_t>(position)];
      detailScroll = std::max(0, std::min(maxScroll, position - visible + 1));
      updateRequired = true;
    }
    return true;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm) && selectedIndex >= 0) {
    const MenuEntry& selected = menuItems[static_cast<size_t>(selectedIndex)];
    if (selected.type == SettingType::TOGGLE) {
      selected.change(0);
      selectedIndex = -1;
    } else if (selected.type == SettingType::ENUM || selected.type == SettingType::VALUE) {
      openSelectorForSelected();
      selectedIndex = -1;
    } else if (selected.type == SettingType::ACTION) {
      selected.change(0);
      selectedIndex = -1;
    }
    return true;
  }
  return false;
}

void CategorySettingsActivity::renderGroupPage() {
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  constexpr int titleFont = MONTSERRAT_16_FONT_ID;
  const int itemFont = systemFontId();
  constexpr int rowHeight = Page::LIST_ITEM_HEIGHT;
  const int listTop = navigation::Menu::height + 20;
  const int visible = std::max(1, (pageHeight - listTop - 10) / rowHeight);

  std::vector<int> rows;
  detailRows(rows);
  const int maxScroll = std::max(0, static_cast<int>(rows.size()) - visible);
  detailScroll = std::max(0, std::min(detailScroll, maxScroll));

  const char* title = "Settings";
  for (const MenuEntry& entry : menuItems) {
    if (entry.type == SettingType::SEPARATOR && entry.group == detailGroup && entry.name) {
      title = entry.name;
      break;
    }
  }
  renderer.text.render(titleFont, 20, 20, title, true, EpdFontFamily::BOLD);
  renderer.bitmap.icon(Close, pageWidth - 60, 20, 40, 40);

  for (int i = 0; i < visible && detailScroll + i < static_cast<int>(rows.size()); ++i) {
    const int index = rows[static_cast<size_t>(detailScroll + i)];
    const MenuEntry& entry = menuItems[static_cast<size_t>(index)];
    const int itemY = listTop + i * rowHeight;
    const bool selected = index == selectedIndex;
    if (selected) {
      renderer.rectangle.fill(0, itemY, pageWidth, rowHeight, static_cast<int>(GfxRenderer::FillTone::Ink));
    }
    const int textY = itemY + (rowHeight - renderer.text.getLineHeight(itemFont)) / 2;
    renderer.text.render(itemFont, 20, textY, entry.name ? entry.name : "", !selected, EpdFontFamily::REGULAR);
    if (entry.type == SettingType::TOGGLE && entry.valuePtr) {
      ReaderFontSettingsDraw::drawToggleCheckbox(renderer, pageWidth - 24, itemY, rowHeight, selected,
                                                  SETTINGS.*(entry.valuePtr) != 0);
    } else {
      const char* value = entry.getValueText ? entry.getValueText() : "";
      if (value && value[0] != '\0') {
        const int valueWidth = renderer.text.getWidth(itemFont, value);
        renderer.text.render(itemFont, pageWidth - valueWidth - 30, textY, value, !selected,
                             EpdFontFamily::REGULAR);
      }
    }
    if (i + 1 < visible && detailScroll + i + 1 < static_cast<int>(rows.size())) {
      renderer.line.render(0, itemY + rowHeight - 1, pageWidth, itemY + rowHeight - 1, !selected,
                           LineRender::Style::Dotted);
    }
  }
}

/**
 * @brief Sets up the menu structure based on expansion states
 */
void CategorySettingsActivity::setupMenu() {
  menuItems.clear();

  for (int i = 0; i < settingsCount; i++) {
    const auto& setting = settingsList[i];
    const SettingInfo* const settingPtr = &setting;

    if (setting.type == SettingType::SEPARATOR) {
      MenuEntry entry;
      entry.name = setting.name;
      entry.type = SettingType::SEPARATOR;
      entry.group = setting.group;
      entry.valuePtr = nullptr;
      entry.valueRange = {0, 0, 0};
      entry.setting = settingPtr;
      entry.getValueText = []() -> const char* { return "›"; };
      entry.change = [](int) {};
      menuItems.push_back(entry);
    } else {
      if (setting.group == GroupType::NONE || isGroupExpanded(setting.group)) {
        MenuEntry entry;
        entry.name = setting.name;
        entry.type = setting.type;
        entry.valuePtr = setting.valuePtr;
        entry.valueRange = setting.valueRange;
        entry.group = setting.group;
        entry.setting = settingPtr;

        if (setting.type == SettingType::INFO) {
          entry.getValueText = [settingPtr]() -> const char* {
            return settingPtr->enumValues.empty() ? "" : settingPtr->enumValues[0].c_str();
          };
          entry.change = [](int) {};
        }
        if (setting.type == SettingType::TOGGLE) {
          entry.getValueText = [settingPtr]() -> const char* {
            return (SETTINGS.*(settingPtr->valuePtr)) ? "ON" : "OFF";
          };
          entry.change = [this, settingPtr](int) {
            SETTINGS.*(settingPtr->valuePtr) = !(SETTINGS.*(settingPtr->valuePtr));
            SETTINGS.saveToFile();
            updateRequired = true;
          };
        }
        if (setting.type == SettingType::ENUM) {
          if (setting.name != nullptr && strcmp(setting.name, "Font Family") == 0) {
            entry.getValueText = [settingPtr]() -> const char* {
              thread_local std::string tls;
              tls = FontManager::readerFontFamilyLabel(SETTINGS.*(settingPtr->valuePtr));
              return tls.c_str();
            };
            entry.change = [this, settingPtr](int delta) {
              int current = SETTINGS.*(settingPtr->valuePtr);
              const int n = static_cast<int>(FontManager::readerFontFamilyOptionCount());
              if (n <= 0) {
                return;
              }
              int newVal = current + delta;
              if (newVal < 0) {
                newVal = n - 1;
              }
              if (newVal >= n) {
                newVal = 0;
              }
              SETTINGS.*(settingPtr->valuePtr) = static_cast<uint8_t>(newVal);
              SETTINGS.saveToFile();
              updateRequired = true;
            };
          } else {
            entry.getValueText = [settingPtr]() -> const char* {
              const int current = SETTINGS.*(settingPtr->valuePtr);
              if (!settingPtr->enumOptionValues.empty() &&
                  settingPtr->enumOptionValues.size() == settingPtr->enumValues.size()) {
                for (size_t i = 0; i < settingPtr->enumOptionValues.size(); ++i) {
                  if (settingPtr->enumOptionValues[i] == current) {
                    return settingPtr->enumValues[i].c_str();
                  }
                }
                if (settingPtr->valuePtr == &SystemSetting::recentLibraryMode &&
                    (current == SystemSetting::RECENT_LIST_DEPRECATED || current == SystemSetting::RECENT_SIMPLE)) {
                  for (size_t i = 0; i < settingPtr->enumOptionValues.size(); ++i) {
                    if (settingPtr->enumOptionValues[i] == SystemSetting::RECENT_FLOW) {
                      return settingPtr->enumValues[i].c_str();
                    }
                  }
                }
                return "Unknown";
              }
              if (current >= 0 && current < (int)settingPtr->enumValues.size()) {
                return settingPtr->enumValues[current].c_str();
              }
              return "Unknown";
            };
            entry.change = [this, settingPtr](int delta) {
              if (!settingPtr->enumOptionValues.empty() &&
                  settingPtr->enumOptionValues.size() == settingPtr->enumValues.size()) {
                int currentIndex = 0;
                const int current = SETTINGS.*(settingPtr->valuePtr);
                for (size_t i = 0; i < settingPtr->enumOptionValues.size(); ++i) {
                  if (settingPtr->enumOptionValues[i] == current) {
                    currentIndex = static_cast<int>(i);
                    break;
                  }
                }
                if (settingPtr->valuePtr == &SystemSetting::recentLibraryMode &&
                    (current == SystemSetting::RECENT_LIST_DEPRECATED || current == SystemSetting::RECENT_SIMPLE)) {
                  for (size_t i = 0; i < settingPtr->enumOptionValues.size(); ++i) {
                    if (settingPtr->enumOptionValues[i] == SystemSetting::RECENT_FLOW) {
                      currentIndex = static_cast<int>(i);
                      break;
                    }
                  }
                }
                int newIndex = currentIndex + delta;
                if (newIndex < 0) newIndex = static_cast<int>(settingPtr->enumOptionValues.size()) - 1;
                if (newIndex >= static_cast<int>(settingPtr->enumOptionValues.size())) newIndex = 0;
                SETTINGS.*(settingPtr->valuePtr) = settingPtr->enumOptionValues[static_cast<size_t>(newIndex)];
                markManualTimezoneSelection(settingPtr->valuePtr);
              } else {
                int current = SETTINGS.*(settingPtr->valuePtr);
                int newVal = current + delta;
                if (newVal < 0) newVal = settingPtr->enumValues.size() - 1;
                if (newVal >= (int)settingPtr->enumValues.size()) newVal = 0;
                SETTINGS.*(settingPtr->valuePtr) = newVal;
                markManualTimezoneSelection(settingPtr->valuePtr);
              }
              SETTINGS.saveToFile();
              updateRequired = true;
            };
          }
        }
        if (setting.type == SettingType::VALUE) {
          entry.getValueText = [settingPtr]() -> const char* {
            static char buffer[32];
            snprintf(buffer, sizeof(buffer), "%d", SETTINGS.*(settingPtr->valuePtr));
            return buffer;
          };
          entry.change = [this, settingPtr](int delta) {
            int current = SETTINGS.*(settingPtr->valuePtr);
            int newVal = current + (delta * settingPtr->valueRange.step);
            if (newVal < settingPtr->valueRange.min) newVal = settingPtr->valueRange.max;
            if (newVal > settingPtr->valueRange.max) newVal = settingPtr->valueRange.min;
            SETTINGS.*(settingPtr->valuePtr) = newVal;
            SETTINGS.saveToFile();
            updateRequired = true;
          };
        }
        if (setting.type == SettingType::ACTION) {
          entry.getValueText = []() -> const char* { return ""; };
          entry.change = [this, settingPtr](int) {
            if (strcmp(settingPtr->name, "Index your library") == 0) {
              if (onIndexLibrary) {
                onIndexLibrary();
              }
              return;
            }
            if (strcmp(settingPtr->name, "Generate thumbnails") == 0) {
              exitActivity();
              enterNewActivity(new ThumbnailGeneratorActivity(renderer, mappedInput, [this] {
                exitActivity();
                updateRequired = true;
              }));
              return;
            }
            if (strcmp(settingPtr->name, "Generate Authors") == 0) {
              exitActivity();
              enterNewActivity(new AuthorGeneratorActivity(renderer, mappedInput, [this] {
                exitActivity();
                updateRequired = true;
              }));
              return;
            }
            if (strcmp(settingPtr->name, "Delete Cache") == 0) {
              exitActivity();
              enterNewActivity(new ClearCacheActivity(renderer, mappedInput, [this] {
                exitActivity();
                updateRequired = true;
              }));
              return;
            }
            if (strcmp(settingPtr->name, "Choose sleep image") == 0) {
              exitActivity();
              enterNewActivity(new SleepImagePickerActivity(renderer, mappedInput, [this] {
                exitActivity();
                updateRequired = true;
              }));
              return;
            }
            if (strcmp(settingPtr->name, "Theme") == 0) {
              exitActivity();
              enterNewActivity(new ThemePickerActivity(renderer, mappedInput, [this] {
                exitActivity();
                updateRequired = true;
              }));
              return;
            }
            if (strcmp(settingPtr->name, "Choose clock") == 0 || strcmp(settingPtr->name, "Face") == 0) {
              exitActivity();
              enterNewActivity(new ClockStylePickerActivity(renderer, mappedInput, [this] {
                exitActivity();
                updateRequired = true;
              }));
              return;
            }
            if (strcmp(settingPtr->name, "Sync") == 0) {
              exitActivity();
              enterNewActivity(new TimeSyncActivity(renderer, mappedInput, [this] {
                exitActivity();
                updateRequired = true;
              }));
              return;
            }
            updateRequired = true;
          };
        }

        menuItems.push_back(entry);
      }
    }
  }
}

/**
 * @brief Applies a delta change to the currently selected menu item
 */
void CategorySettingsActivity::applyChange(int delta) {
  if (selectedIndex < 0 || selectedIndex >= (int)menuItems.size()) return;
  const auto& selected = menuItems[selectedIndex];
  if (selected.type == SettingType::SEPARATOR) return;

  if (selected.type == SettingType::ACTION) return;
  selected.change(delta);
}

int CategorySettingsActivity::selectedOptionIndex(const MenuEntry& entry) const {
  if (!entry.valuePtr) {
    return 0;
  }
  const auto* setting = entry.setting;
  const int current = SETTINGS.*(entry.valuePtr);
  if (entry.type == SettingType::VALUE) {
    const int step = std::max(1, static_cast<int>(entry.valueRange.step));
    return std::max(0, (current - static_cast<int>(entry.valueRange.min)) / step);
  }
  if (entry.type == SettingType::ENUM && setting && !setting->enumOptionValues.empty() &&
      setting->enumOptionValues.size() == setting->enumValues.size()) {
    for (size_t i = 0; i < setting->enumOptionValues.size(); ++i) {
      if (setting->enumOptionValues[i] == current) {
        return static_cast<int>(i);
      }
    }
    if (entry.valuePtr == &SystemSetting::recentLibraryMode &&
        (current == SystemSetting::RECENT_LIST_DEPRECATED || current == SystemSetting::RECENT_SIMPLE)) {
      for (size_t i = 0; i < setting->enumOptionValues.size(); ++i) {
        if (setting->enumOptionValues[i] == SystemSetting::RECENT_FLOW) {
          return static_cast<int>(i);
        }
      }
    }
    return 0;
  }
  return std::max(0, current);
}

void CategorySettingsActivity::applySelectedOption(MenuEntry& entry, const int optionIndex) {
  if (!entry.valuePtr) {
    return;
  }
  const auto* setting = entry.setting;
  if (entry.type == SettingType::VALUE) {
    const int step = std::max(1, static_cast<int>(entry.valueRange.step));
    const int value = static_cast<int>(entry.valueRange.min) + optionIndex * step;
    SETTINGS.*(entry.valuePtr) = static_cast<uint8_t>(
        std::max(static_cast<int>(entry.valueRange.min), std::min(static_cast<int>(entry.valueRange.max), value)));
  } else if (setting && !setting->enumOptionValues.empty() &&
             setting->enumOptionValues.size() == setting->enumValues.size() && optionIndex >= 0 &&
             optionIndex < static_cast<int>(setting->enumOptionValues.size())) {
    SETTINGS.*(entry.valuePtr) = setting->enumOptionValues[static_cast<size_t>(optionIndex)];
  } else {
    SETTINGS.*(entry.valuePtr) = static_cast<uint8_t>(optionIndex);
  }
  markManualTimezoneSelection(entry.valuePtr);
  SETTINGS.saveToFile();
}

void CategorySettingsActivity::openSelectorForSelected() {
  if (selectedIndex < 0 || selectedIndex >= static_cast<int>(menuItems.size())) {
    return;
  }
  const auto& entry = menuItems[selectedIndex];
  if (entry.type != SettingType::ENUM && entry.type != SettingType::VALUE && entry.type != SettingType::TOGGLE) {
    return;
  }

  selectorOptions.clear();
  selectorValues.clear();
  if (entry.type == SettingType::TOGGLE) {
    selectorOptions = {"Off", "On"};
  } else if (entry.type == SettingType::ENUM) {
    if (entry.name != nullptr && strcmp(entry.name, "Font Family") == 0) {
      selectorOptions = FontManager::readerFontFamilyEnumLabels();
    } else if (entry.setting) {
      selectorOptions = entry.setting->enumValues;
    }
  } else {
    const int step = std::max(1, static_cast<int>(entry.valueRange.step));
    for (int value = entry.valueRange.min; value <= entry.valueRange.max; value += step) {
      char buffer[16];
      std::snprintf(buffer, sizeof(buffer), "%d", value);
      selectorOptions.emplace_back(buffer);
    }
  }
  if (selectorOptions.empty()) {
    return;
  }

  selectorMode = kSelectorModeSetting;
  selectorOpen = true;
  selectorSourceIndex = selectedIndex;
  selectorSelectedIndex = std::min(selectedOptionIndex(entry), static_cast<int>(selectorOptions.size()) - 1);
  selectorScrollOffset = std::max(0, selectorSelectedIndex - 2);
  updateRequired = true;
}

bool CategorySettingsActivity::rebuildSleepImageIndex() {
  SdMan.mkdir("/.system");

  std::vector<std::pair<std::string, std::string>> images;
  auto dir = SdMan.open("/sleep");
  if (dir && dir.isDirectory()) {
    char name[256];
    while (auto file = dir.openNextFile()) {
      file.getName(name, sizeof(name));
      std::string filename = name;
      if (!filename.empty() && filename[0] != '.' && isSupportedSleepImageFile(filename)) {
        images.emplace_back(filename, filename);
      }
      file.close();
    }
    dir.close();
  }

  std::sort(images.begin(), images.end(),
            [](const std::pair<std::string, std::string>& a, const std::pair<std::string, std::string>& b) {
              return a.first < b.first;
            });

  if (SdMan.exists("/sleep.bmp")) {
    images.emplace_back("/sleep.bmp", "sleep.bmp (SD root)");
  }
  if (SdMan.exists("/sleep.jpg")) {
    images.emplace_back("/sleep.jpg", "sleep.jpg (SD root)");
  }
  if (SdMan.exists("/sleep.jpeg")) {
    images.emplace_back("/sleep.jpeg", "sleep.jpeg (SD root)");
  }

  FsFile idxFile;
  if (!SdMan.openFileForWrite("SLP", kSleepImageIndexPath, idxFile)) {
    return false;
  }
  for (const auto& image : images) {
    writeString(idxFile, image.first);
    idxFile.write('\t');
    writeString(idxFile, image.second);
    idxFile.write('\n');
  }
  idxFile.close();
  return true;
}

void CategorySettingsActivity::loadSleepImageIndexRows() {
  if (!SdMan.exists(kSleepImageIndexPath)) {
    rebuildSleepImageIndex();
  }

  FsFile idxFile;
  if (!SdMan.openFileForRead("SLP", kSleepImageIndexPath, idxFile)) {
    return;
  }

  std::string line;
  while (idxFile.available()) {
    const int c = idxFile.read();
    if (c < 0) {
      break;
    }
    if (c == '\n' || c == '\r') {
      if (!line.empty()) {
        const size_t tab = line.find('\t');
        if (tab != std::string::npos) {
          selectorValues.push_back(line.substr(0, tab));
          selectorOptions.push_back(line.substr(tab + 1));
        }
        line.clear();
      }
      continue;
    }
    line.push_back(static_cast<char>(c));
  }
  if (!line.empty()) {
    const size_t tab = line.find('\t');
    if (tab != std::string::npos) {
      selectorValues.push_back(line.substr(0, tab));
      selectorOptions.push_back(line.substr(tab + 1));
    }
  }
  idxFile.close();
}

void CategorySettingsActivity::openSleepImageSelector() {
  selectorMode = kSelectorModeSleepImage;
  selectorSourceIndex = selectedIndex;
  selectorOptions.clear();
  selectorValues.clear();
  selectorOptions.emplace_back("Refresh");
  selectorValues.emplace_back(kSleepImageRefreshValue);
  selectorOptions.emplace_back("Random");
  selectorValues.emplace_back("");
  loadSleepImageIndexRows();

  selectorSelectedIndex = 1;
  for (size_t i = 0; i < selectorValues.size(); ++i) {
    if (selectorValues[i] == SETTINGS.sleepCustomBmp) {
      selectorSelectedIndex = static_cast<int>(i);
      break;
    }
  }
  selectorScrollOffset = std::max(0, selectorSelectedIndex - 2);
  selectorOpen = true;
  updateRequired = true;
}

void CategorySettingsActivity::applySleepImageSelection() {
  if (selectorSelectedIndex < 0 || selectorSelectedIndex >= static_cast<int>(selectorValues.size())) {
    return;
  }
  const std::string value = selectorValues[selectorSelectedIndex];
  SETTINGS.setSleepCustomBmpFromInput(value.c_str());
  SETTINGS.saveToFile();
}

void CategorySettingsActivity::moveSelector(const int delta) {
  if (!selectorOpen || selectorOptions.empty()) {
    return;
  }
  selectorSelectedIndex += delta;
  if (selectorSelectedIndex < 0) {
    selectorSelectedIndex = static_cast<int>(selectorOptions.size()) - 1;
  }
  if (selectorSelectedIndex >= static_cast<int>(selectorOptions.size())) {
    selectorSelectedIndex = 0;
  }
  constexpr int visibleRows = 5;
  if (selectorSelectedIndex < selectorScrollOffset) {
    selectorScrollOffset = selectorSelectedIndex;
  } else if (selectorSelectedIndex >= selectorScrollOffset + visibleRows) {
    selectorScrollOffset = selectorSelectedIndex - visibleRows + 1;
  }
  updateRequired = true;
}

void CategorySettingsActivity::selectorPage(const int delta) {
  constexpr int pageRows = 5;
  moveSelector(delta * pageRows);
}

void CategorySettingsActivity::closeSelector(const bool save) {
  if (!selectorOpen) {
    return;
  }
  if (save && selectorMode == kSelectorModeSleepImage) {
    if (selectorSelectedIndex >= 0 && selectorSelectedIndex < static_cast<int>(selectorValues.size()) &&
        selectorValues[selectorSelectedIndex] == kSleepImageRefreshValue) {
      rebuildSleepImageIndex();
      openSleepImageSelector();
      return;
    }
    applySleepImageSelection();
  } else if (save && selectorSourceIndex >= 0 && selectorSourceIndex < static_cast<int>(menuItems.size())) {
    applySelectedOption(menuItems[selectorSourceIndex], selectorSelectedIndex);
  }
  selectorOpen = false;
  selectorMode = kSelectorModeSetting;
  selectorSourceIndex = -1;
  selectorSelectedIndex = 0;
  selectorScrollOffset = 0;
  selectorOptions.clear();
  selectorValues.clear();
  updateRequired = true;
}

/**
 * @brief Main loop handling input and state updates
 */
void CategorySettingsActivity::loop() {
  if (subActivity) {
    subActivity->loop();
    return;
  }

  if (selectorOpen) {
    if (mappedInput.hasTouch() && (mappedInput.wasTouchSwipeUp() || mappedInput.wasTouchSwipeDown())) {
      const int visibleRows =
          embedded && !groupOpen
              ? std::max(1, std::min(static_cast<int>(selectorOptions.size()),
                                    (renderer.getScreenHeight() - (navigation::Menu::height + 20) - 10) /
                                        UiLayout::LIST_ITEM_HEIGHT))
              : PopUp::bounds(renderer, static_cast<int>(selectorOptions.size()),
                              navigation::Menu::height + 20 + Page::LIST_ITEM_HEIGHT + 10 + kEmbeddedListTopExtra)
                    .rows;
      const int maxScroll = std::max(0, static_cast<int>(selectorOptions.size()) - visibleRows);
      const int page = std::max(1, visibleRows);
      if (mappedInput.wasTouchSwipeUp()) {
        selectorScrollOffset = std::min(selectorScrollOffset + page, maxScroll);
      } else {
        selectorScrollOffset = std::max(0, selectorScrollOffset - page);
      }
      selectorSelectedIndex = std::min(selectorScrollOffset, static_cast<int>(selectorOptions.size()) - 1);
      updateRequired = true;
      return;
    }

    if (embedded && !groupOpen && mappedInput.hasTouch()) {
      float tapNx = 0.0f;
      float tapNy = 0.0f;
      if (mappedInput.wasTouchTapInScreen(renderer, tapNx, tapNy)) {
        const int pageWidth = renderer.getScreenWidth();
        const int pageHeight = renderer.getScreenHeight();
        const int tapX = static_cast<int>(tapNx * pageWidth);
        const int tapY = static_cast<int>(tapNy * pageHeight);
        const int listTop = navigation::Menu::height + 20;
        const int rowHeight = Page::LIST_ITEM_HEIGHT;
        const int rows = std::max(0, std::min(static_cast<int>(selectorOptions.size()),
                                              (pageHeight - listTop - 10) / rowHeight));

        if (tapX >= pageWidth - 60 && tapX < pageWidth - 20 && tapY >= 20 && tapY < 60) {
          closeSelector(false);
          return;
        }
        if (tapY >= listTop && tapY < listTop + rows * rowHeight) {
          const int optionIndex = selectorScrollOffset + (tapY - listTop) / rowHeight;
          if (optionIndex >= 0 && optionIndex < static_cast<int>(selectorOptions.size())) {
            selectorSelectedIndex = optionIndex;
            closeSelector(true);
            return;
          }
        }

        closeSelector(false);
        return;
      }
    }

    if (mappedInput.hasTouch()) {
      float tapNx = 0.0f, tapNy = 0.0f;
      if (mappedInput.wasTouchTapInScreen(renderer, tapNx, tapNy)) {
        const int pageWidth = renderer.getScreenWidth();
        const int pageHeight = renderer.getScreenHeight();
        const int contentTop = navigation::Menu::height + 20 + Page::LIST_ITEM_HEIGHT + 10 + kEmbeddedListTopExtra;
        const PopUpBounds box = PopUp::bounds(renderer, static_cast<int>(selectorOptions.size()), contentTop);
        const int tapX = static_cast<int>(tapNx * pageWidth);
        const int tapY = static_cast<int>(tapNy * pageHeight);
        const int maxScroll = std::max(0, static_cast<int>(selectorOptions.size()) - box.rows);
        selectorScrollOffset = std::max(0, std::min(selectorScrollOffset, maxScroll));
        const int optionY = tapY - box.y - box.header;
        if (tapX >= box.x && tapX < box.x + box.width && optionY >= 0 && optionY < box.rows * box.row) {
          const int optionIndex = selectorScrollOffset + optionY / box.row;
          if (optionIndex >= 0 && optionIndex < static_cast<int>(selectorOptions.size())) {
            selectorSelectedIndex = optionIndex;
            closeSelector(true);
            return;
          }
        }
        closeSelector(false);
        return;
      }
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      closeSelector(false);
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      closeSelector(true);
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
      moveSelector(-1);
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
      moveSelector(1);
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
      selectorPage(-1);
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
      selectorPage(1);
      return;
    }
    return;
  }

  if (groupOpen) {
    groupInput();
    return;
  }

  const bool upPressed = mappedInput.wasPressed(itemPrevButton());
  const bool downPressed = mappedInput.wasPressed(itemNextButton());
  const bool leftPressed = mappedInput.wasPressed(tabPrevButton());
  const bool rightPressed = mappedInput.wasPressed(tabNextButton());
  bool confirmPressed = mappedInput.wasPressed(MappedInputManager::Button::Confirm);
  const bool backPressed = mappedInput.wasPressed(MappedInputManager::Button::Back);
  bool touchItem = false;

  if (mappedInput.hasTouch()) {
    if (mappedInput.wasTouchSwipeUp()) {
      const int maxScroll = std::max(0, static_cast<int>(menuItems.size()) - itemsPerPage);
      scrollOffset = std::min(scrollOffset + itemsPerPage, maxScroll);
      selectedIndex = -1;
      updateRequired = true;
      return;
    }
    if (mappedInput.wasTouchSwipeDown()) {
      scrollOffset = std::max(0, scrollOffset - itemsPerPage);
      selectedIndex = -1;
      updateRequired = true;
      return;
    }
  }

  if (mappedInput.hasTouch()) {
    float tapNx = 0.0f, tapNy = 0.0f;
    if (mappedInput.wasTouchTapInScreen(renderer, tapNx, tapNy)) {
      const int tapX = static_cast<int>(tapNx * renderer.getScreenWidth());
      const int tapY = static_cast<int>(tapNy * renderer.getScreenHeight());
      const int startY = navigation::Menu::height + 20 + Page::LIST_ITEM_HEIGHT + 10 + kEmbeddedListTopExtra;
      const int tappedIndex =
          TouchHitTest::uniformListIndex(tapY, startY, UiLayout::LIST_ITEM_HEIGHT, itemsPerPage, scrollOffset,
                                         static_cast<int>(menuItems.size()));
      if (tappedIndex >= 0) {
        const MenuEntry& entry = menuItems[static_cast<size_t>(tappedIndex)];
        if (entry.type == SettingType::ENUM && entry.name && strcmp(entry.name, "Font Size") == 0) {
          constexpr int iconSize = 30;
          constexpr int gap = 8;
          const int textX = entry.group == GroupType::NONE ? 20 : 28;
          const int valueLeft = std::max(textX + 88, renderer.getScreenWidth() * 38 / 100);
          const int valueRight = renderer.getScreenWidth() - 24;
          const char* value = entry.getValueText();
          const int valueWidth = renderer.text.getWidth(systemFontId(), value, EpdFontFamily::REGULAR);
          const int width = iconSize + gap + valueWidth + gap + iconSize;
          const int x = std::max(valueLeft, valueRight - width);
          const int rightX = x + iconSize + gap + valueWidth + gap;

          if (tapX >= x && tapX < x + iconSize) {
            entry.change(-1);
            selectedIndex = -1;
            updateRequired = true;
            return;
          }
          if (tapX >= rightX && tapX < rightX + iconSize) {
            entry.change(1);
            selectedIndex = -1;
            updateRequired = true;
            return;
          }
        }
        selectedIndex = tappedIndex;
        confirmPressed = true;
        touchItem = true;
      }
    }
  }

  if (leftPressed) {
    int newTabIndex = (tabSelectorIndex - 1 + tabCount) % tabCount;
    tabSelectorIndex = newTabIndex;

    if (newTabIndex != 2) {
      navigateToSelectedMenu();
      return;
    }

    updateRequired = true;
    return;
  }

  if (rightPressed) {
    int newTabIndex = (tabSelectorIndex + 1) % tabCount;
    tabSelectorIndex = newTabIndex;

    if (newTabIndex != 2) {
      navigateToSelectedMenu();
      return;
    }
    updateRequired = true;
    return;
  }

  if (handleTabBarTouch(mappedInput, renderer)) {
    return;
  }

  if (backPressed) {
    if (onHardwareBack) {
      onHardwareBack();
    } else {
      onGoBack();
    }
    return;
  }

  bool needRedraw = false;

  if (upPressed) {
    const int totalItems = static_cast<int>(menuItems.size());
    if (totalItems > 0) {
      selectedIndex = (selectedIndex - 1 + totalItems) % totalItems;
      const int maxScroll = std::max(0, totalItems - itemsPerPage);
      if (selectedIndex < scrollOffset) scrollOffset = selectedIndex;
      if (selectedIndex >= scrollOffset + itemsPerPage) scrollOffset = std::min(selectedIndex - itemsPerPage + 1, maxScroll);
      scrollOffset = std::max(0, std::min(scrollOffset, maxScroll));
      needRedraw = true;
    }
  } else if (downPressed) {
    const int totalItems = static_cast<int>(menuItems.size());
    if (totalItems > 0) {
      selectedIndex = (selectedIndex + 1) % totalItems;
      int maxScroll = std::max(0, totalItems - itemsPerPage);
      if (selectedIndex < scrollOffset) scrollOffset = selectedIndex;
      if (selectedIndex > scrollOffset + itemsPerPage - 1) {
        scrollOffset = std::min(selectedIndex - itemsPerPage + 1, maxScroll);
      }
      scrollOffset = std::max(0, std::min(scrollOffset, maxScroll));
      needRedraw = true;
    }
  } else if (confirmPressed) {
    if (selectedIndex >= 0 && selectedIndex < (int)menuItems.size()) {
      const auto& selected = menuItems[selectedIndex];
      if (selected.type == SettingType::SEPARATOR) {
        toggleGroup(selected.group);
        needRedraw = true;
      } else if (selected.type == SettingType::ACTION) {
        selected.change(0);
        needRedraw = true;
      } else if (selected.type == SettingType::INFO) {
      } else if (selected.type == SettingType::TOGGLE) {
        selected.change(0);
        needRedraw = true;
      } else if (selected.type == SettingType::ENUM || selected.type == SettingType::VALUE) {
        openSelectorForSelected();
      } else {
        applyChange(1);
        needRedraw = true;
      }
      if (touchItem) {
        selectedIndex = -1;
      }
    }
  }

  if (needRedraw) {
    updateRequired = true;
  }
}

void CategorySettingsActivity::renderEmbedded() {
  if (!embedded || subActivity) return;
  render();
  updateRequired = false;
}

bool CategorySettingsActivity::takeRenderRequest() {
  if (!embedded || subActivity || !updateRequired) return false;
  updateRequired = false;
  return true;
}

/**
 * @brief Display task loop for periodic rendering
 */
void CategorySettingsActivity::displayTaskLoop() {
  while (true) {
    if (updateRequired && !subActivity) {
      updateRequired = false;
      if (renderingMutex) {
        xSemaphoreTake(renderingMutex, portMAX_DELAY);
        render();
        if (!halfRefreshOnLoadApplied_) {
          halfRefreshOnLoadApplied_ = true;
          SETTINGS.runHalfRefreshOnLoadIfEnabled(renderer, SystemSetting::RefreshOnLoadPage::Settings);
        }
        xSemaphoreGive(renderingMutex);
      }
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void CategorySettingsActivity::renderSelectorOverlay() {
  if (!selectorOpen || selectorOptions.empty()) {
    return;
  }

  if (embedded && !groupOpen) {
    const int pageWidth = renderer.getScreenWidth();
    const int pageHeight = renderer.getScreenHeight();
    const int titleFont = systemFontId();
    const int itemFont = systemFontId();
    constexpr int rowHeight = Page::LIST_ITEM_HEIGHT;
    const int listTop = navigation::Menu::height + 20;
    const int rows = std::max(0, std::min(static_cast<int>(selectorOptions.size()),
                                          (pageHeight - listTop - 10) / rowHeight));
    const int maxScroll = std::max(0, static_cast<int>(selectorOptions.size()) - rows);
    selectorScrollOffset = std::max(0, std::min(selectorScrollOffset, maxScroll));

    const char* title = "Select";
    if (selectorSourceIndex >= 0 && selectorSourceIndex < static_cast<int>(menuItems.size()) &&
        menuItems[selectorSourceIndex].name) {
      title = menuItems[selectorSourceIndex].name;
    }
    renderer.text.render(titleFont, 20, 20, title, true, EpdFontFamily::BOLD);
    renderer.bitmap.icon(Close, pageWidth - 60, 20, 40, 40);

    for (int i = 0; i < rows; ++i) {
      const int optionIndex = selectorScrollOffset + i;
      const int rowY = listTop + i * rowHeight;
      const bool selected = optionIndex == selectorSelectedIndex;
      if (selected) {
        renderer.rectangle.fill(0, rowY, pageWidth, rowHeight, static_cast<int>(GfxRenderer::FillTone::Ink));
      }
      const int textY = rowY + (rowHeight - renderer.text.getLineHeight(itemFont)) / 2;
      renderer.text.render(itemFont, 20, textY, selectorOptions[optionIndex].c_str(), !selected,
                           EpdFontFamily::REGULAR);
      if (i + 1 < rows) {
        renderer.line.render(0, rowY + rowHeight - 1, pageWidth, rowY + rowHeight - 1, !selected,
                             LineRender::Style::Dotted);
      }
    }
    return;
  }

  const int pageHeight = renderer.getScreenHeight();
  const int contentTop = navigation::Menu::height + 20 + Page::LIST_ITEM_HEIGHT + 10 + kEmbeddedListTopExtra;
  const PopUpBounds box = PopUp::bounds(renderer, static_cast<int>(selectorOptions.size()), contentTop);
  const int maxScroll = std::max(0, static_cast<int>(selectorOptions.size()) - box.rows);
  selectorScrollOffset = std::max(0, std::min(selectorScrollOffset, maxScroll));

  const char* title = "Select";
  if (selectorSourceIndex >= 0 && selectorSourceIndex < static_cast<int>(menuItems.size()) &&
      menuItems[selectorSourceIndex].name) {
    title = menuItems[selectorSourceIndex].name;
  }
  std::vector<std::string> options = selectorOptions;
  PopUp::background(renderer, box);
  PopUp::title(renderer, box, title);
  PopUp::list(renderer, box, options, selectorSelectedIndex, selectorScrollOffset);
  PopUp::border(renderer, box);
}

/**
 * @brief Render the category settings screen
 */
void CategorySettingsActivity::render() {
  renderer.clearScreen();

  if (embedded && selectorOpen) {
    if (groupOpen) {
      renderGroupPage();
    }
    renderSelectorOverlay();
    return;
  }

  if (embedded && groupOpen) {
    renderGroupPage();
    return;
  }

  const auto pageWidth = renderer.getScreenWidth();

  int startY = navigation::Menu::height + 20 + Page::LIST_ITEM_HEIGHT + 10 +
               kEmbeddedListTopExtra;

  const char* backLbl = selectorOpen ? "Cancel" : (backButtonLabel ? backButtonLabel : "\xC2\xAB Back");
  const char* confirmLbl = selectorOpen ? "Select" : "Open";
  const char* prevLbl = selectorOpen ? "Page -" : "";
  const char* nextLbl = selectorOpen ? "Page +" : "";
  const auto labels = mappedInput.mapLabels(backLbl, confirmLbl, prevLbl, nextLbl);

  constexpr int itemHeight = Page::LIST_ITEM_HEIGHT;

  int visibleCount = 0;
  for (int i = 0; i < itemsPerPage && (i + scrollOffset) < (int)menuItems.size(); i++) {
    int index = i + scrollOffset;
    const auto& entry = menuItems[index];

    if (entry.type == SettingType::SEPARATOR && (entry.name == nullptr || entry.name[0] == '\0')) {
      continue;
    }

    int itemY = startY + (visibleCount * itemHeight);
    bool isSelected = (index == selectedIndex);
    const bool hasNextItem = index + 1 < static_cast<int>(menuItems.size());

    if (entry.type == SettingType::SEPARATOR) {
      if (isSelected) {
        renderer.rectangle.fill(0, itemY, pageWidth, itemHeight, static_cast<int>(GfxRenderer::FillTone::Ink));
      }

      int textX = 20;
      int textY = itemY + (itemHeight - renderer.text.getLineHeight(systemFontId())) / 2;
      renderer.text.render(systemFontId(), textX, textY, entry.name, !isSelected);

      const char* indicator = entry.getValueText();
      if (indicator && indicator[0] != '\0') {
        int indicatorW = renderer.text.getWidth(systemFontId(), indicator);
        const int indicatorY = itemY + (itemHeight - renderer.text.getLineHeight(systemFontId())) / 2;
        renderer.text.render(systemFontId(), pageWidth - indicatorW - 30, indicatorY, indicator,
                             !isSelected);
      }

      if (hasNextItem) {
        renderer.line.render(0, itemY + itemHeight - 1, pageWidth, itemY + itemHeight - 1, true,
                             LineRender::Style::Dotted);
      }
      visibleCount++;
      continue;
    }

    if (isSelected) {
      renderer.rectangle.fill(0, itemY, pageWidth, itemHeight, static_cast<int>(GfxRenderer::FillTone::Ink));
    }

    int textX = entry.group == GroupType::NONE ? 20 : 28;
    int textY = itemY + (itemHeight - renderer.text.getLineHeight(systemFontId())) / 2;

    renderer.text.render(systemFontId(), textX, textY, entry.name, !isSelected);

    const bool useCheckbox = (entry.type == SettingType::TOGGLE && entry.valuePtr);
    if (useCheckbox) {
      ReaderFontSettingsDraw::drawToggleCheckbox(renderer, pageWidth - 24, itemY, itemHeight, isSelected,
                                                 SETTINGS.*(entry.valuePtr) != 0);
    } else if (entry.type == SettingType::ENUM && entry.name && strcmp(entry.name, "Font Family") == 0) {
      const char* val = entry.getValueText();
      if (val && val[0] != '\0') {
        ReaderFontSettingsDraw::drawFontFamilyRowValue(renderer, READER_SETTINGS.fontFamily, pageWidth - 24, itemY, itemHeight,
                                                       isSelected, val);
      }
    } else if (entry.type == SettingType::ENUM && entry.name && strcmp(entry.name, "Font Size") == 0) {
      const int valueAreaLeft = std::max(textX + 88, pageWidth * 38 / 100);
      drawValueStepper(renderer, entry.getValueText(), valueAreaLeft, pageWidth - 24, itemY, itemHeight);
    } else {
      const char* val = entry.getValueText();
      if (val && val[0] != '\0') {
        int valW = renderer.text.getWidth(systemFontId(), val);
        const int valY = itemY + (itemHeight - renderer.text.getLineHeight(systemFontId())) / 2;
        renderer.text.render(systemFontId(), pageWidth - valW - 30, valY, val, !isSelected);
      }
    }

    if (hasNextItem) {
      renderer.line.render(0, itemY + itemHeight - 1, pageWidth, itemY + itemHeight - 1, true,
                           LineRender::Style::Dotted);
    }
    visibleCount++;
  }

  if ((int)menuItems.size() > itemsPerPage) {
    int listHeight = itemsPerPage * itemHeight;
    int thumbH = (itemsPerPage * listHeight) / menuItems.size();
    int thumbY = startY + (scrollOffset * listHeight) / menuItems.size();
    renderer.rectangle.fill(pageWidth - 4, thumbY, 2, thumbH, true);
  }

  if (selectorOpen) {
    renderSelectorOverlay();
  }

}
