#include "SleepWallpaperActivity.h"

#include <GfxRenderer.h>
#include <SDCardManager.h>

#include <algorithm>
#include <string>
#include <vector>

#include "activity/page/SubPage.h"
#include "activity/page/components/global/PopUp.h"
#include "activity/page/components/global/Toggle.h"
#include "activity/settings/SleepImagePickerActivity.h"
#include "images/LibraryFilterRight.h"
#include "state/SystemSetting.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "util/StringUtils.h"

namespace {
bool inside(const int x, const int y, const int left, const int top, const int width, const int height) {
  return x >= left && x < left + width && y >= top && y < top + height;
}

std::string firstSleepImagePath() {
  std::vector<std::string> paths;
  auto scanDirectory = [&](auto&& scan, const std::string& directoryPath) -> void {
    auto dir = SdMan.open(directoryPath.c_str());
    if (!dir || !dir.isDirectory()) {
      if (dir) dir.close();
      return;
    }

    char name[256] = {};
    while (auto file = dir.openNextFile()) {
      file.getName(name, sizeof(name));
      const std::string filename = name;
      if (filename.empty() || filename[0] == '.') {
        file.close();
        continue;
      }
      const std::string path = directoryPath == "/" ? "/" + filename : directoryPath + "/" + filename;
      if (file.isDirectory()) {
        file.close();
        scan(scan, path);
      } else if (StringUtils::checkFileExtension(filename, ".bmp") ||
                 StringUtils::checkFileExtension(filename, ".jpg") ||
                 StringUtils::checkFileExtension(filename, ".jpeg") ||
                 StringUtils::checkFileExtension(filename, ".png")) {
        paths.push_back(path);
        file.close();
      } else {
        file.close();
      }
      yield();
    }
    dir.close();
  };

  scanDirectory(scanDirectory, "/sleep");
  if (SdMan.exists("/sleep.bmp")) paths.emplace_back("/sleep.bmp");
  if (SdMan.exists("/sleep.jpg")) paths.emplace_back("/sleep.jpg");
  if (SdMan.exists("/sleep.jpeg")) paths.emplace_back("/sleep.jpeg");
  if (SdMan.exists("/sleep.png")) paths.emplace_back("/sleep.png");
  std::sort(paths.begin(), paths.end());
  return paths.empty() ? std::string() : paths.front();
}
}

void SleepWallpaperActivity::onEnter() {
  ActivityWithSubactivity::onEnter();
  if (bookCover_) {
    transparent_ = false;
    SETTINGS.sleepScreen = SystemSetting::COVER;
    SETTINGS.saveToFile();
    render();
    return;
  }
  transparent_ = SETTINGS.sleepScreen == SystemSetting::TRANSPARENT;
  SETTINGS.sleepScreen = transparent_ ? SystemSetting::TRANSPARENT : SystemSetting::CUSTOM;
  SETTINGS.saveToFile();
  render();
}

void SleepWallpaperActivity::render() {
  renderer.clearScreen();
  const bool coverSettings = bookCover_ || SETTINGS.sleepScreen == SystemSetting::COVER;
  contentTop_ = SubPage::header(renderer, coverSettings ? "Book Cover" : "Wallpaper", "Sleep");
  const int font = systemFontId();
  const int width = renderer.getScreenWidth();
  const int qualityRow = coverSettings ? 0 : 3;
  const int coverModeRow = coverSettings ? 1 : 4;
  const int rowCount = coverSettings ? 2 : 5;

  for (int row = 0; row < rowCount; ++row) {
    const int y = contentTop_ + row * kRowHeight;
    renderer.rectangle.fill(0, y, width, kRowHeight, static_cast<int>(GfxRenderer::FillTone::Paper));
    if (row != 0) {
      renderer.line.render(SettingsListLayout::kDividerInset, y - 1,
                           width - SettingsListLayout::kDividerInset, y - 1, true,
                           LineRender::Style::Dotted);
    }
  }

  if (!coverSettings) {
    const int textY = contentTop_ + (kRowHeight - renderer.text.getLineHeight(font)) / 2;
    renderer.text.render(font, 20, textY, "Choose an image", true, EpdFontFamily::REGULAR);
    renderer.bitmap.icon(LibraryFilterRight, width - 56, contentTop_ + (kRowHeight - 30) / 2, 32, 30);

    const int randomY = contentTop_ + kRowHeight;
    renderer.text.render(font, 20, randomY + (kRowHeight - renderer.text.getLineHeight(font)) / 2, "Random", true,
                         EpdFontFamily::REGULAR);
    Toggle::render(renderer, width - 24, randomY, kRowHeight, SETTINGS.sleepCustomBmp[0] == '\0', false);

    const int transparentY = contentTop_ + kRowHeight * 2;
    renderer.text.render(font, 20, transparentY + (kRowHeight - renderer.text.getLineHeight(font)) / 2,
                         "Transparent", true, EpdFontFamily::REGULAR);
    Toggle::render(renderer, width - 24, transparentY, kRowHeight, transparent_, false);
  }

  const int qualityY = contentTop_ + kRowHeight * qualityRow;
  renderer.text.render(font, 20, qualityY + (kRowHeight - renderer.text.getLineHeight(font)) / 2,
                       "Quality", true, EpdFontFamily::REGULAR);
  static constexpr const char* labels[] = {"Low", "Medium", "High"};
  const int quality = std::min<int>(SETTINGS.sleepImageQuality, 2);
  const int valueWidth = renderer.text.getWidth(font, labels[quality]);
  renderer.text.render(font, width - valueWidth - 30,
                       qualityY + (kRowHeight - renderer.text.getLineHeight(font)) / 2, labels[quality], true,
                       EpdFontFamily::REGULAR);

  const int coverModeY = contentTop_ + kRowHeight * coverModeRow;
  renderer.text.render(font, 20, coverModeY + (kRowHeight - renderer.text.getLineHeight(font)) / 2,
                       "Cover mode", true, EpdFontFamily::REGULAR);
  const char* coverMode = SETTINGS.sleepScreenCoverMode == SystemSetting::CROP ? "Crop" : "Fill";
  const int coverModeWidth = renderer.text.getWidth(font, coverMode);
  renderer.text.render(font, width - coverModeWidth - 30,
                       coverModeY + (kRowHeight - renderer.text.getLineHeight(font)) / 2, coverMode, true,
                       EpdFontFamily::REGULAR);

  if (qualityPopup_) renderQualityPopup();
  if (coverModePopup_) renderCoverModePopup();

  renderer.displayBuffer();
}

void SleepWallpaperActivity::renderQualityPopup() {
  const std::vector<std::string> values = {"Low", "Medium", "High"};
  const PopUpBounds box = PopUp::bounds(renderer, static_cast<int>(values.size()));
  PopUp::background(renderer, box);
  PopUp::title(renderer, box, "Quality");
  PopUp::list(renderer, box, values, std::min<int>(SETTINGS.sleepImageQuality, 2), 0);
  PopUp::border(renderer, box);
}

void SleepWallpaperActivity::renderCoverModePopup() {
  const std::vector<std::string> values = {"Fill", "Crop"};
  const PopUpBounds box = PopUp::bounds(renderer, static_cast<int>(values.size()));
  PopUp::background(renderer, box);
  PopUp::title(renderer, box, "Cover mode");
  PopUp::list(renderer, box, values, std::min<int>(SETTINGS.sleepScreenCoverMode, 1), 0);
  PopUp::border(renderer, box);
}

void SleepWallpaperActivity::openImagePicker() {
  enterNewActivity(new SleepImagePickerActivity(renderer, mappedInput, [this] {
    exitActivity();
    render();
  }, {}, {}, "Choose an image", false));
}

void SleepWallpaperActivity::close() {
  if (onBack_) onBack_();
}

void SleepWallpaperActivity::handleTouch(const int x, const int y) {
  const bool coverSettings = bookCover_ || SETTINGS.sleepScreen == SystemSetting::COVER;
  const int qualityRow = coverSettings ? 0 : 3;
  const int coverModeRow = coverSettings ? 1 : 4;

  if (qualityPopup_) {
    const PopUpBounds box = PopUp::bounds(renderer, 3);
    if (!inside(x, y, box.x, box.y, box.width, box.height)) {
      qualityPopup_ = false;
      render();
      return;
    }
    const int optionY = y - box.y - box.header;
    if (optionY < 0 || optionY >= box.rows * box.row) return;
    SETTINGS.sleepImageQuality = static_cast<uint8_t>(optionY / box.row);
    SETTINGS.saveToFile();
    qualityPopup_ = false;
    render();
    return;
  }
  if (coverModePopup_) {
    const PopUpBounds box = PopUp::bounds(renderer, 2);
    if (!inside(x, y, box.x, box.y, box.width, box.height)) {
      coverModePopup_ = false;
      render();
      return;
    }
    const int optionY = y - box.y - box.header;
    if (optionY < 0 || optionY >= box.rows * box.row) return;
    SETTINGS.sleepScreenCoverMode = static_cast<uint8_t>(optionY / box.row);
    SETTINGS.saveToFile();
    coverModePopup_ = false;
    render();
    return;
  }
  if (!coverSettings && inside(x, y, 0, contentTop_, renderer.getScreenWidth(), kRowHeight)) {
    openImagePicker();
    return;
  }
  if (!coverSettings && inside(x, y, 0, contentTop_ + kRowHeight, renderer.getScreenWidth(), kRowHeight)) {
    if (SETTINGS.sleepCustomBmp[0] == '\0') {
      const std::string path = firstSleepImagePath();
      if (!path.empty()) SETTINGS.setSleepCustomBmpFromInput(path.c_str());
    } else {
      SETTINGS.setSleepCustomBmpFromInput("");
    }
    SETTINGS.saveToFile();
    render();
    return;
  }
  if (!coverSettings && inside(x, y, 0, contentTop_ + kRowHeight * 2, renderer.getScreenWidth(), kRowHeight)) {
    transparent_ = !transparent_;
    SETTINGS.sleepScreen = transparent_ ? SystemSetting::TRANSPARENT : SystemSetting::CUSTOM;
    SETTINGS.saveToFile();
    render();
    return;
  }
  if (inside(x, y, 0, contentTop_ + kRowHeight * qualityRow, renderer.getScreenWidth(), kRowHeight)) {
    qualityPopup_ = true;
    render();
    return;
  }
  if (inside(x, y, 0, contentTop_ + kRowHeight * coverModeRow, renderer.getScreenWidth(), kRowHeight)) {
    coverModePopup_ = true;
    render();
  }
}

void SleepWallpaperActivity::loop() {
  if (subActivity) {
    subActivity->loop();
    return;
  }
  if (qualityPopup_) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      qualityPopup_ = false;
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
      SETTINGS.sleepImageQuality = static_cast<uint8_t>((SETTINGS.sleepImageQuality + delta) % 3);
      SETTINGS.saveToFile();
      render();
    }
    return;
  }
  if (coverModePopup_) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      coverModePopup_ = false;
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
      SETTINGS.sleepScreenCoverMode = SETTINGS.sleepScreenCoverMode == SystemSetting::FIT
                                          ? SystemSetting::CROP
                                          : SystemSetting::FIT;
      SETTINGS.saveToFile();
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
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    openImagePicker();
  }
}
