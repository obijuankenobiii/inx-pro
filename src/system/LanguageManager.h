#pragma once

#include <string>
#include <vector>

/** Runtime UI translation loader for /system/lang/<code>/translate.yml. */
class LanguageManager {
 public:
  struct LanguageInfo {
    std::string code;
    std::string name;
  };
  /** Loads the language selected in SystemSetting, if the SD card is ready. */
  static void initialize();

  /** Selects an installed language, or clears the selection with an empty code. */
  static bool setLanguage(const char* code);

  /** Returns the active language code, or an empty string for English. */
  static const char* activeLanguageCode();

  /** Returns the installed package display name when available. */
  static const char* activeLanguageName();

  /** True when the package has translation metadata on the SD card. */
  static bool isInstalled(const char* code);

  /** Returns installed language packages, with the empty-code Default option first. */
  static std::vector<LanguageInfo> installedLanguages();

  /** Returns a display name for a language code, or the code when no metadata is found. */
  static std::string languageNameForCode(const char* code);

  /** Sets the preferred language package for the current book; empty means follow the UI language. */
  static void setBookLanguage(const char* code);

  /** Returns the current book's preferred language, falling back to the active UI language. */
  static const char* bookLanguageCode();

  /**
   * Translates a known system label while preserving the supplied English
   * fallback for missing keys and for dynamic/book text.
   */
  static const char* translateText(const char* englishText);

 private:
  LanguageManager() = delete;
};
