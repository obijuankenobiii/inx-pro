#include "DictionaryPickerActivity.h"
#include "system/UiLayout.h"

#include <GfxRenderer.h>
#include <SDCardManager.h>
#include <esp_task_wdt.h>

#include <algorithm>

#include "activity/page/SubPage.h"
#include "state/ReaderSetting.h"
#include "state/SystemSetting.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "util/SdIoMutex.h"
#include "util/StringUtils.h"

namespace {
constexpr int kRowH = UiLayout::LIST_ITEM_HEIGHT;
constexpr const char* kDictionariesRoot = "/dictionaries";

/** A folder counts as a dictionary if it directly contains at least one .idx and one .dict file. */
bool folderLooksLikeDictionary(const std::string& folderPath) {
  INX_SERIAL.printf("[%lu] [DICT-PICKER] inspect folder='%s'\n", millis(), folderPath.c_str());
  FsFile dir = SdMan.open(folderPath.c_str());
  if (!dir || !dir.isDirectory()) {
    if (dir) {
      dir.close();
    }
    return false;
  }
  bool hasIdx = false;
  bool hasDict = false;
  for (FsFile file = dir.openNextFile(); file; file = dir.openNextFile()) {
    if (!file.isDirectory()) {
      char name[160] = {};
      file.getName(name, sizeof(name));
      if (name[0] == '.') {
        file.close();
        continue;
      }
      if (StringUtils::checkFileExtension(std::string(name), ".idx")) {
        hasIdx = true;
      } else if (StringUtils::checkFileExtension(std::string(name), ".dict")) {
        hasDict = true;
      }
    }
    file.close();
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(1));
    if (hasIdx && hasDict) {
      break;
    }
  }
  dir.close();
  INX_SERIAL.printf("[%lu] [DICT-PICKER] inspect result folder='%s' idx=%d dict=%d\n", millis(), folderPath.c_str(),
                hasIdx ? 1 : 0, hasDict ? 1 : 0);
  return hasIdx && hasDict;
}
}

void DictionaryPickerActivity::onEnter() {
  INX_SERIAL.printf("[%lu] [DICT-PICKER] onEnter start\n", millis());
  ActivityWithSubactivity::onEnter();
  scanDictionaryFolders();
  INX_SERIAL.printf("[%lu] [DICT-PICKER] scan complete folders=%u\n", millis(),
                static_cast<unsigned>(folders_.size()));
  render();
  INX_SERIAL.printf("[%lu] [DICT-PICKER] onEnter render complete\n", millis());
}

void DictionaryPickerActivity::scanDictionaryFolders() {
  SdIoMutex::Lock ioLock;
  folders_.clear();
  selectedIndex_ = 0;
  selectedVisible_ = false;
  scrollOffset_ = 0;

  FsFile dir = SdMan.open(kDictionariesRoot);
  if (!dir || !dir.isDirectory()) {
    INX_SERIAL.printf("[%lu] [DICT-PICKER] root open failed path='%s'\n", millis(), kDictionariesRoot);
    if (dir) {
      dir.close();
    }
    return;
  }

  for (FsFile file = dir.openNextFile(); file; file = dir.openNextFile()) {
    if (file.isDirectory()) {
      char name[160] = {};
      file.getName(name, sizeof(name));
      const std::string folderPath = std::string(kDictionariesRoot) + "/" + name;
      INX_SERIAL.printf("[%lu] [DICT-PICKER] found folder='%s'\n", millis(), folderPath.c_str());
      if (folderLooksLikeDictionary(folderPath)) {
        folders_.push_back(name);
      }
    }
    file.close();
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  dir.close();

  std::sort(folders_.begin(), folders_.end());

  if (READER_SETTINGS.dictionaryFolder[0] != '\0') {
    const auto it = std::find(folders_.begin(), folders_.end(), std::string(READER_SETTINGS.dictionaryFolder));
    if (it != folders_.end()) {
      selectedIndex_ = static_cast<int>(std::distance(folders_.begin(), it));
    }
  }
}

void DictionaryPickerActivity::loop() {
  if (SubPage::closeInput(renderer, mappedInput, goBack_)) return;

  if (mappedInput.hasTouch()) {
    float tapNx = 0.0f;
    float tapNy = 0.0f;
    if (mappedInput.wasTouchTapInScreen(renderer, tapNx, tapNy)) {
      const int screenH = renderer.getScreenHeight();
      const int bodyTop = UiLayout::PAGE_HEADER_HEIGHT;
      const int visibleRows = std::max(1, (screenH - 44 - bodyTop) / kRowH);
      const int tapY = static_cast<int>(tapNy * screenH);
      if (tapY >= bodyTop && tapY < bodyTop + visibleRows * kRowH) {
        const int tappedIndex = scrollOffset_ + (tapY - bodyTop) / kRowH;
        if (tappedIndex >= 0 && tappedIndex < static_cast<int>(folders_.size())) {
          selectedIndex_ = tappedIndex;
          const std::string& chosen = folders_[static_cast<size_t>(selectedIndex_)];
          strncpy(READER_SETTINGS.dictionaryFolder, chosen.c_str(), sizeof(READER_SETTINGS.dictionaryFolder) - 1);
          READER_SETTINGS.dictionaryFolder[sizeof(READER_SETTINGS.dictionaryFolder) - 1] = '\0';
          READER_SETTINGS.saveToFile();
          goBack_();
          return;
        }
      }
    }
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    goBack_();
    return;
  }

  const int total = static_cast<int>(folders_.size());
  if (total == 0) {
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    selectedIndex_ = (selectedIndex_ + 1) % total;
    selectedVisible_ = true;
    render();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    selectedIndex_ = (selectedIndex_ + total - 1) % total;
    selectedVisible_ = true;
    render();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    const std::string& chosen = folders_[static_cast<size_t>(selectedIndex_)];
    strncpy(READER_SETTINGS.dictionaryFolder, chosen.c_str(), sizeof(READER_SETTINGS.dictionaryFolder) - 1);
    READER_SETTINGS.dictionaryFolder[sizeof(READER_SETTINGS.dictionaryFolder) - 1] = '\0';
    READER_SETTINGS.saveToFile();
    goBack_();
    return;
  }
}

void DictionaryPickerActivity::render() {
  INX_SERIAL.printf("[%lu] [DICT-PICKER] render start folders=%u\n", millis(),
                static_cast<unsigned>(folders_.size()));
  renderer.clearScreen();
  INX_SERIAL.printf("[%lu] [DICT-PICKER] after clear\n", millis());
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const int bodyTop = SubPage::header(renderer, "Choose dictionary");
  INX_SERIAL.printf("[%lu] [DICT-PICKER] after header bodyTop=%d\n", millis(), bodyTop);

  const int total = static_cast<int>(folders_.size());
  if (total == 0) {
    const int centerY = bodyTop + (screenH - bodyTop - 80) / 2;
    renderer.text.centered(systemFontId(), centerY, "No dictionaries found.", true, EpdFontFamily::BOLD);
    renderer.text.centered(systemFontId(), centerY + 32, "Put StarDict folders under /dictionaries/", true,
                           EpdFontFamily::REGULAR);
    const auto hints = mappedInput.mapLabels("\xC2\xAB Back", "", "", "");
    renderer.displayBuffer();
    INX_SERIAL.printf("[%lu] [DICT-PICKER] render empty complete\n", millis());
    return;
  }

  const int listBottom = screenH - 44;
  const int visibleRows = std::max(1, (listBottom - bodyTop) / kRowH);
  if (selectedIndex_ < scrollOffset_) {
    scrollOffset_ = selectedIndex_;
  } else if (selectedIndex_ >= scrollOffset_ + visibleRows) {
    scrollOffset_ = selectedIndex_ - visibleRows + 1;
  }
  const int maxScroll = std::max(0, total - visibleRows);
  scrollOffset_ = std::max(0, std::min(scrollOffset_, maxScroll));
  const int endIndex = std::min(total, scrollOffset_ + visibleRows);

  for (int i = scrollOffset_; i < endIndex; ++i) {
    const int y = bodyTop + (i - scrollOffset_) * kRowH;
    const bool selected = selectedVisible_ && i == selectedIndex_;
    const bool active = folders_[static_cast<size_t>(i)] == READER_SETTINGS.dictionaryFolder;
    if (selected) {
      renderer.rectangle.fill(0, y, screenW, kRowH, static_cast<int>(GfxRenderer::FillTone::Ink));
    }
    INX_SERIAL.printf("[%lu] [DICT-PICKER] row background index=%d selected=%d active=%d\n", millis(), i,
                      selected ? 1 : 0, active ? 1 : 0);
    const int font = systemFontId();
    const int titleY = y + (kRowH - renderer.text.getLineHeight(font)) / 2;
    std::string label = folders_[static_cast<size_t>(i)];
    if (active) {
      label += "  *";
    }
    INX_SERIAL.printf("[%lu] [DICT-PICKER] before row text font=%d y=%d label='%s'\n", millis(), font, titleY,
                      label.c_str());
    renderer.text.render(font, 20, titleY, label.c_str(), !selected, EpdFontFamily::REGULAR);
    INX_SERIAL.printf("[%lu] [DICT-PICKER] row drawn index=%d label='%s'\n", millis(), i, label.c_str());
    if (i + 1 < endIndex) {
      renderer.line.render(0, y + kRowH - 1, screenW, y + kRowH - 1, true, LineRender::Style::Dotted);
    }
  }

  const auto hints = mappedInput.mapLabels("\xC2\xAB Back", "Select", "Up", "Down");
  INX_SERIAL.printf("[%lu] [DICT-PICKER] before display rows=%d\n", millis(), endIndex - scrollOffset_);
  renderer.displayBuffer();
  INX_SERIAL.printf("[%lu] [DICT-PICKER] render list complete rows=%d\n", millis(), endIndex - scrollOffset_);
}
