#pragma once

/**
 * @file SystemSetting.h
 * @brief Public interface and types for SystemSetting.
 */




#include <cstddef>
#include <cstdint>
#include <iosfwd>

class GfxRenderer;

/**
 * @brief System settings management class
 */
class SystemSetting {
 private:
  SystemSetting() = default;
  static SystemSetting instance;

 public:
  SystemSetting(const SystemSetting&) = delete;
  SystemSetting& operator=(const SystemSetting&) = delete;

  /**
   * @brief Sleep screen display modes
   */
  enum SLEEP_SCREEN_MODE {
    DARK = 0,
    LIGHT = 1,
    CUSTOM = 2,
    COVER = 3,
    TRANSPARENT = 4,
    BLANK = 5,
    DATETIME_LEGACY = 6,
    WIDGET = 7,
    SLEEP_SCREEN_MODE_COUNT
  };

  enum SLEEP_CLOCK_STYLE {
    CLOCK_STACKED_CITY = 0,
    CLOCK_CENTERED_DATE = 1,
    CLOCK_HORIZONTAL_CARD = 2,
    CLOCK_WEEKDAYS_TIME = 3,
    SLEEP_CLOCK_STYLE_COUNT
  };

  enum CLOCK_TIME_FORMAT { CLOCK_12_HOUR = 0, CLOCK_24_HOUR = 1, CLOCK_TIME_FORMAT_COUNT };

  /**
   * @brief Sleep screen cover scaling modes
   */
  enum SLEEP_SCREEN_COVER_MODE {
    FIT = 0,
    CROP = 1,
    SLEEP_SCREEN_COVER_MODE_COUNT
  };

  enum SLEEP_IMAGE_QUALITY {
    SLEEP_IMAGE_LOW = 0,
    SLEEP_IMAGE_MEDIUM = 1,
    SLEEP_IMAGE_HIGH = 2,
    SLEEP_IMAGE_QUALITY_COUNT
  };

  /**
   * @brief Status bar item types for configurable sections
   */
  enum STATUS_BAR_ITEM {
    STATUS_ITEM_NONE = 0,
    STATUS_ITEM_PAGE_NUMBERS = 1,
    STATUS_ITEM_PERCENTAGE = 2,
    STATUS_ITEM_CHAPTER_TITLE = 3,
    STATUS_ITEM_BATTERY_ICON = 4,
    STATUS_ITEM_BATTERY_PERCENTAGE = 5,
    STATUS_ITEM_BATTERY_ICON_WITH_PERCENT = 6,
    STATUS_ITEM_PROGRESS_BAR = 7,
    STATUS_ITEM_PROGRESS_BAR_WITH_PERCENT = 8,
    STATUS_ITEM_PAGE_BARS = 9,
    STATUS_ITEM_BOOK_TITLE = 10,
    STATUS_ITEM_AUTHOR_NAME = 11,
    STATUS_ITEM_PAGE_NUMBERS_WITH_PERCENT = 12,
    STATUS_ITEM_TIME_LEFT_CHAPTER = 13,
    STATUS_ITEM_TIME_LEFT_BOOK = 14,
    STATUS_BAR_ITEM_COUNT
  };

  /**
   * @brief Text-reader status bar mode
   */
  enum STATUS_BAR_MODE {
    NONE = 0,
    NO_PROGRESS = 1,
    FULL = 2,
    FULL_WITH_PROGRESS_BAR = 3,
    ONLY_PROGRESS_BAR = 4,
    BATTERY_PERCENTAGE = 5,
    PERCENTAGE = 6,
    PAGE_BARS = 7,
    STATUS_BAR_MODE_COUNT
  };

  /**
   * @brief Screen orientation modes
   */
  enum ORIENTATION {
    PORTRAIT = 0,
    LANDSCAPE_CW = 1,
    INVERTED = 2,
    LANDSCAPE_CCW = 3,
    ORIENTATION_COUNT
  };

  /**
   * @brief Side button layout configurations
   */
  enum SIDE_BUTTON_LAYOUT {
    PREV_NEXT = 0,
    NEXT_PREV = 1,
    SIDE_BUTTON_LAYOUT_COUNT
  };

  /**
   * @brief Reader direction mapping for navigation
   */
  enum READER_DIRECTION_MAPPING {
    MAP_LEFT_RIGHT = 0,
    MAP_RIGHT_LEFT = 1,
    MAP_UP_DOWN = 2,
    MAP_DOWN_UP = 3,
    MAP_NONE = 4,
    READER_DIRECTION_MAPPING_COUNT
  };

  /**
   * @brief Which buttons navigate the main menu tab bar vs the page items.
   */
  enum MAIN_MENU_NAV {
    MAIN_MENU_NAV_FRONT = 0,
    MAIN_MENU_NAV_SIDE = 1,
    MAIN_MENU_NAV_COUNT
  };

  /**
   * @brief System UI text size options.
   *
   * These affect the device UI only; reader font sizing remains in ReaderSetting.
   */
  enum SYSTEM_TEXT_SIZE {
    SYSTEM_TEXT_SMALL = 0,
    SYSTEM_TEXT_MEDIUM = 1,
    SYSTEM_TEXT_LARGE = 2,
    SYSTEM_TEXT_SIZE_COUNT
  };

  /**
   * @brief Font family options used by reader and system-font settings.
   * @details 0 is the built-in font; 1+ select SD card folders under /fonts (sorted names), see FontManager.
   */
  enum FONT_FAMILY {
    MONTSERRAT = 0,
    FONT_FAMILY_BUILTIN_COUNT,
    FONT_FAMILY_COUNT = FONT_FAMILY_BUILTIN_COUNT
  };

  /**
   * @brief Font size options
   */
  enum FONT_SIZE {
    EXTRA_SMALL = 0,
    SMALL = 1,
    MEDIUM = 2,
    LARGE = 3,
    EXTRA_LARGE = 4,
    FONT_SIZE_COUNT
  };

  /**
   * @brief Paragraph alignment options
   */
  enum PARAGRAPH_ALIGNMENT {
    JUSTIFIED = 0,
    LEFT_ALIGN = 1,
    CENTER_ALIGN = 2,
    RIGHT_ALIGN = 3,
    /** Same role as CrossPoint “Book style”: use EPUB `text-align` / cascade for each block (value must match
     *  EPUB_PARAGRAPH_ALIGNMENT_FOLLOW_CSS in lib/Epub/Epub/parsers/ChapterHtmlSlimParser.h). */
    FOLLOW_CSS = 4,
    PARAGRAPH_ALIGNMENT_COUNT
  };

  /**
   * @brief Sleep timeout duration options
   */
  enum SLEEP_TIMEOUT {
    SLEEP_1_MIN = 0,
    SLEEP_5_MIN = 1,
    SLEEP_10_MIN = 2,
    SLEEP_15_MIN = 3,
    SLEEP_30_MIN = 4,
    SLEEP_NEVER = 5,
    SLEEP_TIMEOUT_COUNT
  };

  /**
   * @brief Screen refresh frequency options
   */
  enum REFRESH_FREQUENCY {
    REFRESH_1 = 0,
    REFRESH_5 = 1,
    REFRESH_10 = 2,
    REFRESH_15 = 3,
    REFRESH_30 = 4,
    REFRESH_OFF = 5,
    REFRESH_FREQUENCY_COUNT
  };

  /** Text-entry keyboard layouts. */
  enum KEYBOARD_LAYOUT {
    KEYBOARD_QWERTY = 0,
    KEYBOARD_NUMPAD = 1,
    KEYBOARD_LAYOUT_COUNT
  };

  /**
   * @brief Global short power button behavior (for library, home, etc.)
   */
  enum SHORT_PWRBTN {
    IGNORE = 0,
    SLEEP = 1,
    PAGE_REFRESH = 2,
    SHORT_PWRBTN_COUNT
  };


  /** Picco's physical slide switch behavior. */
  enum PICCO_TOGGLE_MODE {
    PICCO_TOGGLE_LIGHT = 0,
    PICCO_TOGGLE_POWER = 1,
    PICCO_TOGGLE_DARK_MODE = 2,
    PICCO_TOGGLE_FLICK_PAGE = 3,
    PICCO_TOGGLE_MODE_COUNT
  };






  enum READER_BUTTON_ACTION {
    BTN_ACTION_NONE = 0,
    BTN_ACTION_PAGE_NEXT,
    BTN_ACTION_PAGE_PREVIOUS,
    BTN_ACTION_OPEN_SETTINGS,
    BTN_ACTION_ANNOTATE,
    BTN_ACTION_DICTIONARY,
    BTN_ACTION_PAGE_REFRESH,
    BTN_ACTION_CHAPTER_SKIP_NEXT,
    BTN_ACTION_CHAPTER_SKIP_PREVIOUS,
    BTN_ACTION_BOOKMARK,
    BTN_ACTION_TABLE_OF_CONTENTS,
    BTN_ACTION_CHANGE_ORIENTATION,
    BTN_ACTION_APPLY_PRESET,
    BTN_ACTION_QUICK_ACTIONS,
    BTN_ACTION_GENERATE_FULL_DATA,
    BTN_ACTION_GENERATE_THUMBNAIL,
    BTN_ACTION_GO_TO_PERCENT,
    BTN_ACTION_TOGGLE_LIGHT,
    BTN_ACTION_EXIT,
    READER_BUTTON_ACTION_COUNT
  };

  /** Human-readable label for a READER_BUTTON_ACTION value - shared by the button-action pickers in
   *  ReaderPresetsActivity, the Quick Actions checklist screen, and the in-reader quick-actions popup. */
  static const char* readerButtonActionLabel(uint8_t action);

  /**
   * @brief Battery percentage display options
   */
  enum HIDE_BATTERY_PERCENTAGE {
    HIDE_NEVER = 0,
    HIDE_READER = 1,
    HIDE_ALWAYS = 2,
    HIDE_BATTERY_PERCENTAGE_COUNT
  };

  /**
   * @brief Library browser display modes
   */
  enum LIBRARY_MODE {
    LIBRARY_LIST = 0,
    LIBRARY_GRID = 1,
    LIBRARY_THUMBNAIL = 2,
    LIBRARY_LIST_THUMBNAIL = 3,
    LIBRARY_THUMBNAIL_3X2 = 4,
    LIBRARY_MODE_COUNT
  };

  /**
   * @brief Library browser content view
   */
  enum LIBRARY_VIEW_MODE {
    LIBRARY_VIEW_FOLDERS = 0,
    LIBRARY_VIEW_BOOKS = 1,
    LIBRARY_VIEW_TAGS = 2,
    LIBRARY_VIEW_SHELF = 3,
    LIBRARY_VIEW_FAVORITES = 4,
    LIBRARY_VIEW_READING = 5,
    LIBRARY_VIEW_FINISHED = 6,
    LIBRARY_VIEW_AUTHORS = 7,
    LIBRARY_VIEW_SERIES = 8,
    LIBRARY_VIEW_PLUGIN = 9,
    LIBRARY_VIEW_MODE_COUNT
  };

  /**
   * @brief Thumbnail sizing behavior shared by Library and home widgets.
   */
  enum THUMBNAIL_SIZE {
    THUMBNAIL_ACTUAL = 0,
    THUMBNAIL_EVEN = 1,
    THUMBNAIL_SIZE_COUNT
  };

  /**
   * @brief Boot destination settings
   */
  enum BOOT_SETTING {
    RECENT_PAGE = 0,
    HOME_PAGE = 1,
    BOOT_SETTING_COUNT
  };

  /** X4 Pro capacitive Home button behavior. */
  enum X4PRO_HOME_BUTTON {
    X4PRO_HOME_BUTTON_BACK = 0,
    X4PRO_HOME_BUTTON_HOME = 1,
    X4PRO_HOME_BUTTON_COUNT
  };

  uint8_t sleepScreen = LIGHT;
  uint8_t sleepScreenCoverMode = FIT;
  /** Sleep image quality for the sleep-screen image renderer. */
  uint8_t sleepImageQuality = SLEEP_IMAGE_HIGH;
  /**
   * Fixed custom/transparent sleep image when multiple images exist.
   * Empty = pick a random file from /sleep/ (and /sleep.bmp/.jpg/.jpeg) each time.
   * Basename only = use /sleep/<basename> (e.g. night.bmp / night.jpg).
   * Exactly "/sleep.bmp" (or /sleep.jpg/.jpeg) = use SD-root fallback file only.
   */
  char sleepCustomBmp[64] = "";
  uint8_t sleepClockStyle = CLOCK_CENTERED_DATE;
  uint8_t sleepClockTimeFormat = CLOCK_24_HOUR;
  /** UTC offset in 15-minute steps, biased by +12h. 0=UTC-12:00, 80=UTC+08:00, 104=UTC+14:00. */
  uint8_t timeZoneQuarterOffset = 80;
  /** When enabled, time sync may replace the stored offset using the network lookup. */
  uint8_t timeZoneAutoDetectEnabled = 1;
  /** Persisted IANA timezone selected during the WiFi time-sync flow. Empty uses automatic detection. */
  char timeZoneId[64] = "";
  /** Show the ambient clock (ScreenComponents::drawMenuClock) in the tab-bar chrome. */
  uint8_t showMenuClock = 1;
  /** Hide the ambient clock from the page header. */
  uint8_t hideMenuClock = 0;
  /** Hide the bottom navigation bar and expose it through a bottom-edge swipe grid. */
  uint8_t hiddenMenu = 0;
  /** Show the curved/dithered visual when a swipe exits an activity. */
  uint8_t exitAnimation = 1;
  /** Hide reading statistics from the home shortcuts and statistics entry. */
  uint8_t disableStatistics = 0;
  /** Reserved coordinate slots retained only for settings-file compatibility. */
  uint16_t legacyMenuX = 0;
  uint16_t legacyMenuY = 0;
  /** Keyboard layout used by text-entry activities. */
  uint8_t keyboardLayout = KEYBOARD_QWERTY;
  /** System UI text size: Small=10pt, Medium=12pt, Large=14pt. */
  uint8_t systemTextSize = SYSTEM_TEXT_SMALL;
  /** System UI font family: 0 is Montserrat, 1+ are sorted families under /fonts. */
  uint8_t systemFontFamily = MONTSERRAT;
  /** Persisted Light drawer text/icon theme: 0=light, 1=dark. */
  uint8_t darkMode = 0;

  uint8_t shortPwrBtn = PAGE_REFRESH;


  /** Picco-only physical slide switch behavior; ignored on other devices. */
  uint8_t piccoToggleMode = PICCO_TOGGLE_LIGHT;

  uint8_t sideButtonLayout = PREV_NEXT;

  uint8_t mainMenuNav = MAIN_MENU_NAV_FRONT;
  /** Active SD language package code; empty selects the built-in English UI. */
  char languageCode[33] = "";

  uint8_t sleepTimeout = SLEEP_10_MIN;

  char opdsServerUrl[128] = "";
  char opdsUsername[64] = "";
  char opdsPassword[64] = "";

  /** Hide battery percentage setting. */
  uint8_t hideBatteryPercentage = HIDE_NEVER;
  /** Long-press on prev/next values shared with book and reader settings. */
  static constexpr uint8_t LONG_PRESS_OFF = 0;
  static constexpr uint8_t LONG_PRESS_CHAPTER_SKIP = 1;
  static constexpr uint8_t LONG_PRESS_PAGE_SKIP_5 = 2;
  uint8_t useLibraryIndex = 0;

  uint8_t refreshOnLoadRecent = 0;
  uint8_t refreshOnLoadLibrary = 0;
  uint8_t refreshOnLoadSettings = 0;
  uint8_t refreshOnLoadSync = 0;
  uint8_t refreshOnLoadStatistics = 0;
  uint8_t libraryMode = LIBRARY_GRID;
  uint8_t libraryViewMode = LIBRARY_VIEW_FOLDERS;
  /** Library sort mode persisted when leaving Library (0=Title A–Z … 5=Read Z–A). */
  uint8_t librarySortMode = 0;
  /** Hide finished books from the normal folder and All Books library views. */
  uint8_t hideFinishedBooks = 0;
  /** Shared thumbnail sizing mode for Library and home widgets. */
  uint8_t thumbnailSize = THUMBNAIL_ACTUAL;

  uint8_t bootSetting = RECENT_PAGE;
  uint8_t x4proHomeButton = X4PRO_HOME_BUTTON_BACK;



  /** Book image quality (READER_IMAGE_QUALITY): 0=Low(1-bit), 1=Medium(fast 2-bit grayscale),
   *  2=High(quality 2-bit grayscale with text preserved). */
  enum READER_IMAGE_QUALITY {
    READER_IMAGE_LOW = 0,
    READER_IMAGE_MEDIUM = 1,
    READER_IMAGE_HIGH = 2,
    READER_IMAGE_QUALITY_COUNT
  };
  /** When set, hub thumbnails use rounded clip on `GfxRenderer::drawBitmap` (Recent: sparse ink outside arc; stats:
   * paper). */
  uint8_t bitmapRoundedCorners = 0;
  /** Flick page-turn direction: 0=off, 1=normal direction, 2=inverted direction. */
  uint8_t shakePageTurn = 0;
  /** Motion gesture threshold: 0=low, 1=normal, 2=high sensitivity. */
  uint8_t shakePageTurnSensitivity = 1;

  ~SystemSetting() = default;

  /**
   * @brief Gets the singleton instance
   * @return Reference to the SystemSetting instance
   */
  static SystemSetting& getInstance() { return instance; }

  enum class RefreshOnLoadPage : uint8_t { Recent, Library, Settings, Sync, Statistics };

  /**
   * If the per-page refresh-on-load toggle is on, performs one HALF_REFRESH (no-op when off).
   */
  void runHalfRefreshOnLoadIfEnabled(const GfxRenderer& renderer, RefreshOnLoadPage page) const;

  /**
   * @brief Gets power button duration in milliseconds
   * @return Duration in ms (10ms for sleep, 400ms for ignore)
   */
  uint16_t getPowerButtonDuration() const {
    return shortPwrBtn == SystemSetting::SHORT_PWRBTN::SLEEP ? 10 : 400;
  }

  /**
   * @brief Validates and stores the fixed custom sleep image choice (basename under /sleep/ or SD-root sleep file).
   * @param s nullptr or empty string clears (random selection each sleep).
   */
  void setSleepCustomBmpFromInput(const char* s);

  /**
   * @brief Saves all settings to file
   * @return true if save successful, false otherwise
   */
  bool saveToFile() const;

  /**
   * @brief Loads all settings from file
   * @return true if load successful, false otherwise
   */
  bool loadFromFile();

  /**
   * @brief Gets sleep timeout in milliseconds
   * @return Sleep timeout in milliseconds
   */
  unsigned long getSleepTimeoutMs() const;

  int getTimeZoneOffsetMinutes() const;
  void formatTimeZone(char* out, size_t outSize) const;
};

#define SETTINGS SystemSetting::getInstance()
