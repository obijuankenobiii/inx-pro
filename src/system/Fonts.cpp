#include "Fonts.h"

#ifndef INX_SIMULATOR_WEB_ONLY
#include "system/FontManager.h"
#endif

namespace {

int montserratSystemFontId() {
  switch (SETTINGS.systemTextSize) {
    case SystemSetting::SYSTEM_TEXT_MEDIUM:
      return MONTSERRAT_12_FONT_ID;
    case SystemSetting::SYSTEM_TEXT_LARGE:
      return MONTSERRAT_14_FONT_ID;
    case SystemSetting::SYSTEM_TEXT_SMALL:
    default:
      return MONTSERRAT_10_FONT_ID;
  }
}

int montserratFontIdForPointSize(const int pointSize) {
  if (pointSize >= 50) return MONTSERRAT_CLOCK_70_FONT_ID;
  if (pointSize <= 8) return MONTSERRAT_8_FONT_ID;
  if (pointSize <= 10) return MONTSERRAT_10_FONT_ID;
  if (pointSize <= 12) return MONTSERRAT_12_FONT_ID;
  if (pointSize <= 14) return MONTSERRAT_14_FONT_ID;
  if (pointSize <= 16) return MONTSERRAT_16_FONT_ID;
  return MONTSERRAT_18_FONT_ID;
}

int systemPointSize() {
  switch (SETTINGS.systemTextSize) {
    case SystemSetting::SYSTEM_TEXT_MEDIUM:
      return 12;
    case SystemSetting::SYSTEM_TEXT_LARGE:
      return 14;
    case SystemSetting::SYSTEM_TEXT_SMALL:
    default:
      return 10;
  }
}

}  // namespace

int systemFontIdForPointSize(const int pointSize) {
  const int fallback = montserratFontIdForPointSize(pointSize);

#ifndef INX_SIMULATOR_WEB_ONLY
  if (SETTINGS.systemFontFamily == SystemSetting::MONTSERRAT) {
    return fallback;
  }

  const std::string family = FontManager::readerFontFamilyLabel(SETTINGS.systemFontFamily);
  const int selected = FontManager::getFontIdAtOrBelowPointSize(family, pointSize);
  if (selected < FontManager::SD_FONT_START_ID) {
    return fallback;
  }
  return FontManager::ensureSystemFontReady(selected) ? selected : fallback;
#else
  return fallback;
#endif
}

int systemFontId() { return systemFontIdForPointSize(systemPointSize()); }
