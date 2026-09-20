/**
 * @file ReaderSetting.cpp
 * @brief Definitions for ReaderSetting.
 */

#include "state/ReaderSetting.h"

#include "state/SystemSetting.h"

#ifndef INX_SIMULATOR_WEB_ONLY
#include <GfxRenderer.h>
#include <HalDisplay.h>
#endif
#include <HardwareSerial.h>
#include <SDCardManager.h>
#include <Serialization.h>

#include <cstring>
#include <string>

#ifndef INX_SIMULATOR_WEB_ONLY
#include "system/FontManager.h"
#include "system/Fonts.h"
#endif

void readAndValidate(FsFile& file, uint8_t& member, uint8_t maxValue);

ReaderSetting ReaderSetting::instance;

namespace {
constexpr uint8_t READER_SETTINGS_FILE_VERSION = 1;
constexpr uint8_t READER_SETTINGS_COUNT = 35;
constexpr char READER_SETTINGS_FILE[] = "/.system/reader_settings.bin";
constexpr uint32_t FNV1A_OFFSET = 2166136261UL;
constexpr uint32_t FNV1A_PRIME = 16777619UL;

void hashBytes(uint32_t& hash, const void* data, const size_t len) {
  const auto* bytes = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < len; ++i) {
    hash ^= bytes[i];
    hash *= FNV1A_PRIME;
  }
}

template <typename T>
void hashPod(uint32_t& hash, const T& value) {
  hashBytes(hash, &value, sizeof(T));
}

void hashString(uint32_t& hash, const char* value) {
  const uint32_t len = value == nullptr ? 0 : static_cast<uint32_t>(strlen(value));
  hashPod(hash, len);
  if (len > 0) {
    hashBytes(hash, value, len);
  }
}

bool hashFile(const char* path, uint32_t& hash) {
  FsFile file;
  if (!SdMan.openFileForRead("CPR", path, file)) {
    return false;
  }
  hash = FNV1A_OFFSET;
  uint8_t buffer[64];
  while (file.available()) {
    const int n = file.read(buffer, sizeof(buffer));
    if (n <= 0) {
      break;
    }
    hashBytes(hash, buffer, static_cast<size_t>(n));
  }
  file.close();
  return true;
}

uint32_t readerSettingsHash(const ReaderSetting& settings, const uint8_t fontFamilyToSave) {
  uint32_t hash = FNV1A_OFFSET;
  hashPod(hash, READER_SETTINGS_FILE_VERSION);
  hashPod(hash, READER_SETTINGS_COUNT);
  hashPod(hash, settings.statusBar);
  hashPod(hash, settings.statusBarLeft);
  hashPod(hash, settings.statusBarMiddle);
  hashPod(hash, settings.statusBarRight);
  hashPod(hash, settings.statusBarFullStyle);
  hashPod(hash, settings.extraParagraphSpacing);
  hashPod(hash, settings.textAntiAliasing);
  hashString(hash, settings.dictionaryFolder);
  hashPod(hash, settings.btnPowerShortAction);
  hashPod(hash, settings.orientation);
  hashPod(hash, fontFamilyToSave);
  hashPod(hash, settings.fontSize);
  hashPod(hash, settings.lineHeight);
  hashPod(hash, settings.textSpace);
  hashPod(hash, settings.paragraphAlignment);
  hashPod(hash, settings.paragraphCssIndentEnabled);
  hashPod(hash, settings.refreshFrequency);
  hashPod(hash, settings.hyphenationEnabled);
  hashPod(hash, settings.bionicReadingEnabled);
  hashPod(hash, settings.readingGuideLinesEnabled);
  hashPod(hash, settings.screenMargin);
  hashPod(hash, settings.pageAutoTurnSeconds);
  hashPod(hash, settings.readerImageGrayscale);
  hashPod(hash, settings.readerSmartRefreshOnImages);
  hashPod(hash, settings.longPressChapterSkip);
  hashPod(hash, settings.quickActionsMask);
  hashPod(hash, settings.dailyReadingGoalMinutes);
  hashPod(hash, settings.btnLeftAction);
  hashPod(hash, settings.btnRightAction);
  hashPod(hash, settings.btnLeftLongAction);
  hashPod(hash, settings.btnRightLongAction);
  hashPod(hash, settings.pageTurnMode);
  hashPod(hash, settings.disableLightControl);
  hashPod(hash, settings.doubleTapAction);
  hashString(hash, settings.defaultLanguageCode);
  return hash;
}
}

/**
 * @brief Saves all reader settings to file
 * @return true if save successful, false otherwise
 */
bool ReaderSetting::saveToFile() const {
  uint8_t fontFamilyToSave = fontFamily;
#ifndef INX_SIMULATOR_WEB_ONLY
  FontManager::clampReaderFontFamilySlot(fontFamilyToSave);
  if (fontFamilyToSave != fontFamily) {
    const_cast<ReaderSetting*>(this)->fontFamily = fontFamilyToSave;
  }
  ReaderSetting* mutableSettings = const_cast<ReaderSetting*>(this);
  if (FontManager::isOutlineFontFamilySlot(fontFamilyToSave)) {
    if (mutableSettings->fontSize < FontManager::OUTLINE_FONT_MIN_POINT_SIZE ||
        mutableSettings->fontSize > FontManager::OUTLINE_FONT_MAX_POINT_SIZE) {
      mutableSettings->fontSize = static_cast<uint8_t>(
          FontManager::pointSizeForLegacyReaderSize(mutableSettings->fontSize));
    }
  } else if (mutableSettings->fontSize >= SystemSetting::FONT_SIZE_COUNT) {
    mutableSettings->fontSize = FontManager::legacyReaderSizeForPointSize(mutableSettings->fontSize);
  }
#endif

  const uint32_t currentHash = readerSettingsHash(*this, fontFamilyToSave);
  uint32_t storedHash = 0;
  if (hashFile(READER_SETTINGS_FILE, storedHash) && storedHash == currentHash) {
    return true;
  }

  SdMan.mkdir("/.system");

  FsFile outputFile;
  if (!SdMan.openFileForWrite("CPR", READER_SETTINGS_FILE, outputFile)) {
    return false;
  }

  serialization::writePod(outputFile, READER_SETTINGS_FILE_VERSION);
  serialization::writePod(outputFile, READER_SETTINGS_COUNT);
  serialization::writePod(outputFile, statusBar);
  serialization::writePod(outputFile, statusBarLeft);
  serialization::writePod(outputFile, statusBarMiddle);
  serialization::writePod(outputFile, statusBarRight);
  serialization::writePod(outputFile, statusBarFullStyle);
  serialization::writePod(outputFile, extraParagraphSpacing);
  serialization::writePod(outputFile, textAntiAliasing);
  serialization::writeString(outputFile, std::string(dictionaryFolder));
  serialization::writePod(outputFile, btnPowerShortAction);
  serialization::writePod(outputFile, orientation);
  serialization::writePod(outputFile, fontFamilyToSave);
  serialization::writePod(outputFile, fontSize);
  serialization::writePod(outputFile, lineHeight);
  serialization::writePod(outputFile, textSpace);
  serialization::writePod(outputFile, paragraphAlignment);
  serialization::writePod(outputFile, paragraphCssIndentEnabled);
  serialization::writePod(outputFile, refreshFrequency);
  serialization::writePod(outputFile, hyphenationEnabled);
  serialization::writePod(outputFile, bionicReadingEnabled);
  serialization::writePod(outputFile, screenMargin);
  serialization::writePod(outputFile, pageAutoTurnSeconds);
  serialization::writePod(outputFile, readerImageGrayscale);
  serialization::writePod(outputFile, readerSmartRefreshOnImages);
  serialization::writePod(outputFile, longPressChapterSkip);
  serialization::writePod(outputFile, readingGuideLinesEnabled);
  serialization::writePod(outputFile, quickActionsMask);
  serialization::writePod(outputFile, dailyReadingGoalMinutes);
  serialization::writePod(outputFile, btnLeftAction);
  serialization::writePod(outputFile, btnRightAction);
  serialization::writePod(outputFile, btnLeftLongAction);
  serialization::writePod(outputFile, btnRightLongAction);
  serialization::writePod(outputFile, pageTurnMode);
  serialization::writePod(outputFile, disableLightControl);
  serialization::writePod(outputFile, doubleTapAction);
  serialization::writeString(outputFile, std::string(defaultLanguageCode));

  outputFile.close();

  INX_SERIAL.printf("[%lu] [CPR] Reader settings saved to file (version %u)\n", millis(), READER_SETTINGS_FILE_VERSION);
  return true;
}

/**
 * @brief Loads all reader settings from file
 * @return true if load successful, false otherwise
 */
bool ReaderSetting::loadFromFile() {
  FsFile inputFile;

  if (!SdMan.openFileForRead("CPR", READER_SETTINGS_FILE, inputFile)) {
    saveToFile();
    return false;
  }

  uint8_t version;
  serialization::readPod(inputFile, version);

  if (version != READER_SETTINGS_FILE_VERSION) {
    INX_SERIAL.printf("[%lu] [CPR] Deserialization failed: Unsupported version %u (expected %u)\n", millis(), version,
                      READER_SETTINGS_FILE_VERSION);
    inputFile.close();
    SdMan.remove(READER_SETTINGS_FILE);
    saveToFile();
    return false;
  }

  uint8_t fileSettingsCount = 0;
  serialization::readPod(inputFile, fileSettingsCount);
  if (fileSettingsCount != READER_SETTINGS_COUNT) {
    INX_SERIAL.printf("[%lu] [CPR] Deserialization failed: Expected %u settings, found %u\n", millis(),
                      READER_SETTINGS_COUNT, fileSettingsCount);
    inputFile.close();
    SdMan.remove(READER_SETTINGS_FILE);
    saveToFile();
    return false;
  }
  uint8_t settingsRead = 0;

  do {
    readAndValidate(inputFile, statusBar, SystemSetting::STATUS_BAR_MODE_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    readAndValidate(inputFile, statusBarLeft, SystemSetting::STATUS_BAR_ITEM_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    readAndValidate(inputFile, statusBarMiddle, SystemSetting::STATUS_BAR_ITEM_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    readAndValidate(inputFile, statusBarRight, SystemSetting::STATUS_BAR_ITEM_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    readAndValidate(inputFile, statusBarFullStyle, SystemSetting::STATUS_BAR_ITEM_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    serialization::readPod(inputFile, extraParagraphSpacing);
    if (++settingsRead >= fileSettingsCount) break;

    serialization::readPod(inputFile, textAntiAliasing);
    if (++settingsRead >= fileSettingsCount) break;

    {
      std::string dictFolderStr;
      serialization::readString(inputFile, dictFolderStr);
      strncpy(dictionaryFolder, dictFolderStr.c_str(), sizeof(dictionaryFolder) - 1);
      dictionaryFolder[sizeof(dictionaryFolder) - 1] = '\0';
    }
    if (++settingsRead >= fileSettingsCount) break;

    readAndValidate(inputFile, btnPowerShortAction, SystemSetting::READER_BUTTON_ACTION_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    readAndValidate(inputFile, orientation, SystemSetting::ORIENTATION_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    {
      uint8_t rawFontFamily = 0;
      serialization::readPod(inputFile, rawFontFamily);
      fontFamily = rawFontFamily;
#ifndef INX_SIMULATOR_WEB_ONLY
      FontManager::clampReaderFontFamilySlot(fontFamily);
#endif
    }
    if (++settingsRead >= fileSettingsCount) break;

    serialization::readPod(inputFile, fontSize);
#ifndef INX_SIMULATOR_WEB_ONLY
    if (FontManager::isOutlineFontFamilySlot(fontFamily)) {
      if (fontSize < FontManager::OUTLINE_FONT_MIN_POINT_SIZE ||
          fontSize > FontManager::OUTLINE_FONT_MAX_POINT_SIZE) {
        fontSize = static_cast<uint8_t>(FontManager::pointSizeForLegacyReaderSize(fontSize));
      }
    } else if (fontSize >= SystemSetting::FONT_SIZE_COUNT) {
      // A settings file written while an outline family was selected can be
      // reopened after that family is removed or replaced by a legacy .bin
      // family. Convert the stored point size back to the legacy index.
      fontSize = FontManager::legacyReaderSizeForPointSize(fontSize);
    }
#else
    if (fontSize >= SystemSetting::FONT_SIZE_COUNT) fontSize = SystemSetting::SMALL;
#endif
    if (++settingsRead >= fileSettingsCount) break;

    serialization::readPod(inputFile, lineHeight);
    if (lineHeight < 10 || lineHeight > 200) lineHeight = 100;
    if (++settingsRead >= fileSettingsCount) break;

    serialization::readPod(inputFile, textSpace);
    if (textSpace < 10 || textSpace > 200) textSpace = 100;
    if (++settingsRead >= fileSettingsCount) break;

    readAndValidate(inputFile, paragraphAlignment, SystemSetting::PARAGRAPH_ALIGNMENT_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    serialization::readPod(inputFile, paragraphCssIndentEnabled);
    if (paragraphCssIndentEnabled > 1) paragraphCssIndentEnabled = 1;
    if (++settingsRead >= fileSettingsCount) break;

    readAndValidate(inputFile, refreshFrequency, SystemSetting::REFRESH_FREQUENCY_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    serialization::readPod(inputFile, hyphenationEnabled);
    if (++settingsRead >= fileSettingsCount) break;

    serialization::readPod(inputFile, bionicReadingEnabled);
    if (bionicReadingEnabled > 1) bionicReadingEnabled = 0;
    if (++settingsRead >= fileSettingsCount) break;

    serialization::readPod(inputFile, screenMargin);
    if (++settingsRead >= fileSettingsCount) break;

    serialization::readPod(inputFile, pageAutoTurnSeconds);
    if (pageAutoTurnSeconds > 180 || pageAutoTurnSeconds % 10 != 0) pageAutoTurnSeconds = 0;
    if (++settingsRead >= fileSettingsCount) break;

    serialization::readPod(inputFile, readerImageGrayscale);
    if (readerImageGrayscale >= SystemSetting::READER_IMAGE_QUALITY_COUNT) {
      readerImageGrayscale = SystemSetting::READER_IMAGE_LOW;
    }
    if (++settingsRead >= fileSettingsCount) break;

    serialization::readPod(inputFile, readerSmartRefreshOnImages);
    if (readerSmartRefreshOnImages > 1) readerSmartRefreshOnImages = 1;
    if (++settingsRead >= fileSettingsCount) break;

    serialization::readPod(inputFile, longPressChapterSkip);
    if (longPressChapterSkip > SystemSetting::LONG_PRESS_PAGE_SKIP_5) {
      longPressChapterSkip = SystemSetting::LONG_PRESS_CHAPTER_SKIP;
    }
    if (++settingsRead >= fileSettingsCount) break;

    serialization::readPod(inputFile, readingGuideLinesEnabled);
    if (readingGuideLinesEnabled > 2) readingGuideLinesEnabled = 0;
    if (++settingsRead >= fileSettingsCount) break;

    serialization::readPod(inputFile, quickActionsMask);
    if (++settingsRead >= fileSettingsCount) break;

    serialization::readPod(inputFile, dailyReadingGoalMinutes);
    if (dailyReadingGoalMinutes > 120) dailyReadingGoalMinutes = 5;
    if (++settingsRead >= fileSettingsCount) break;

    readAndValidate(inputFile, btnLeftAction, SystemSetting::READER_BUTTON_ACTION_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    readAndValidate(inputFile, btnRightAction, SystemSetting::READER_BUTTON_ACTION_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    readAndValidate(inputFile, btnLeftLongAction, SystemSetting::READER_BUTTON_ACTION_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    readAndValidate(inputFile, btnRightLongAction, SystemSetting::READER_BUTTON_ACTION_COUNT);
    ++settingsRead;

    if (settingsRead < fileSettingsCount) {
      serialization::readPod(inputFile, pageTurnMode);
      if (pageTurnMode > PAGE_TURN_TAP) pageTurnMode = PAGE_TURN_TAP;
      ++settingsRead;
    }
    if (settingsRead < fileSettingsCount) {
      serialization::readPod(inputFile, disableLightControl);
      if (disableLightControl > 1) disableLightControl = 0;
      ++settingsRead;
    }
    if (settingsRead < fileSettingsCount) {
      readAndValidate(inputFile, doubleTapAction, SystemSetting::READER_BUTTON_ACTION_COUNT);
      ++settingsRead;
    }
    if (settingsRead < fileSettingsCount) {
      std::string languageCode;
      serialization::readString(inputFile, languageCode);
      if (languageCode.size() <= sizeof(defaultLanguageCode) - 1) {
        std::strncpy(defaultLanguageCode, languageCode.c_str(), sizeof(defaultLanguageCode) - 1);
        defaultLanguageCode[sizeof(defaultLanguageCode) - 1] = '\0';
      } else {
        defaultLanguageCode[0] = '\0';
      }
      ++settingsRead;
    }

  } while (false);

  inputFile.close();

#ifndef INX_SIMULATOR_WEB_ONLY
  FontManager::clampReaderFontFamilySlot(fontFamily);
  if (FontManager::isOutlineFontFamilySlot(fontFamily)) {
    if (fontSize < FontManager::OUTLINE_FONT_MIN_POINT_SIZE ||
        fontSize > FontManager::OUTLINE_FONT_MAX_POINT_SIZE) {
      fontSize = static_cast<uint8_t>(FontManager::pointSizeForLegacyReaderSize(fontSize));
    }
  } else if (fontSize >= SystemSetting::FONT_SIZE_COUNT) {
    fontSize = FontManager::legacyReaderSizeForPointSize(fontSize);
  }
#endif

  quickActionsMask &= (1u << SystemSetting::READER_BUTTON_ACTION_COUNT) - 1;
  quickActionsMask &= ~(1u << SystemSetting::BTN_ACTION_NONE);
  quickActionsMask &= ~(1u << SystemSetting::BTN_ACTION_QUICK_ACTIONS);

  if (pageTurnMode > PAGE_TURN_TAP) pageTurnMode = PAGE_TURN_TAP;
  if (disableLightControl > 1) disableLightControl = 0;
  if (doubleTapAction >= SystemSetting::READER_BUTTON_ACTION_COUNT) {
    doubleTapAction = SystemSetting::BTN_ACTION_NONE;
  }

  INX_SERIAL.printf("[%lu] [CPR] Reader settings loaded (version %u, %u items)\n", millis(), version, settingsRead);

  return true;
}

/**
 * @brief Gets reader line compression factor based on font and spacing
 * @return Line compression multiplier
 */
float ReaderSetting::getReaderLineCompression() const {
  uint8_t lh = lineHeight;
  if (lh < 10 || lh > 200) lh = 100;
  return static_cast<float>(lh) / 100.0f;
}

float ReaderSetting::getReaderWordSpacingFactor() const {
  uint8_t ts = textSpace;
  if (ts < 10 || ts > 200) ts = 100;
  return static_cast<float>(ts) / 100.0f;
}

/**
 * @brief Gets screen refresh frequency in pages
 * @return Number of pages between refreshes
 */
int ReaderSetting::getRefreshFrequency() const {
  switch (refreshFrequency) {
    case SystemSetting::REFRESH_1:
      return 1;
    case SystemSetting::REFRESH_5:
      return 5;
    case SystemSetting::REFRESH_10:
      return 10;
    case SystemSetting::REFRESH_15:
      return 15;
    case SystemSetting::REFRESH_30:
      return 30;
    case SystemSetting::REFRESH_OFF:
      return 0;
    default:
      return 0;
  }
}

int ReaderSetting::getReaderFontIdForSettingsUi(uint8_t familySlot, uint8_t sizeIndex) const {
#ifdef INX_SIMULATOR_WEB_ONLY
  (void)familySlot;
  (void)sizeIndex;
  return 0;
#else
  if (familySlot < SystemSetting::FONT_FAMILY_BUILTIN_COUNT) {
    return getReaderFontIdForFamilyAndSize(familySlot, sizeIndex);
  }
  return getReaderFontIdForFamilyAndSize(SystemSetting::MONTSERRAT, sizeIndex);
#endif
}

int ReaderSetting::getReaderFontIdForFamilyAndSize(uint8_t family, uint8_t size) const {
#ifdef INX_SIMULATOR_WEB_ONLY
  (void)family;
  (void)size;
  return 0;
#else
  if (family >= SystemSetting::FONT_FAMILY_BUILTIN_COUNT) {
    const std::string sdName = FontManager::readerFontFamilyLabel(family);
    if (FontManager::isOutlineFontFamily(sdName)) {
      int pointSize = size;
      if (pointSize < FontManager::OUTLINE_FONT_MIN_POINT_SIZE ||
          pointSize > FontManager::OUTLINE_FONT_MAX_POINT_SIZE) {
        pointSize = FontManager::pointSizeForLegacyReaderSize(size);
      }
      return FontManager::getFontId(sdName, pointSize);
    }

    if (size >= SystemSetting::FONT_SIZE_COUNT) {
      size = SystemSetting::MEDIUM;
    }
    const int preferredPt = FontManager::pointSizeForLegacyReaderSize(size);
    return FontManager::getFontIdNearestPointSize(sdName, preferredPt);
  }

  if (size >= SystemSetting::FONT_SIZE_COUNT) {
    size = SystemSetting::MEDIUM;
  }

  switch (family) {
    case SystemSetting::MONTSERRAT:
      switch (size) {
        case SystemSetting::EXTRA_SMALL:
          return MONTSERRAT_10_FONT_ID;
        case SystemSetting::SMALL:
          return MONTSERRAT_12_FONT_ID;
        case SystemSetting::MEDIUM:
        default:
          return MONTSERRAT_14_FONT_ID;
        case SystemSetting::LARGE:
          return MONTSERRAT_16_FONT_ID;
        case SystemSetting::EXTRA_LARGE:
          return MONTSERRAT_18_FONT_ID;
      }
    default:
      return getReaderFontIdForFamilyAndSize(SystemSetting::MONTSERRAT, size);
  }
#endif
}

/**
 * @brief Gets reader font ID based on font family and size
 * @return Font identifier for rendering
 */
int ReaderSetting::getReaderFontId() const {
#ifdef INX_SIMULATOR_WEB_ONLY
  return 0;
#else
  return getReaderFontIdForFamilyAndSize(fontFamily, fontSize);
#endif
}
