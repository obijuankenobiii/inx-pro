#pragma once

/**
 * @file ReaderSetting.h
 * @brief Public interface for ReaderSetting - persisted reader/book-reading preferences.
 *
 * Split out of SystemSetting so reader preferences (font, layout, status bar, per-button reader
 * actions, XTC reader options, refresh cadence, etc.) persist to their own file
 * (/.system/reader_settings.bin) instead of being interleaved with device-level settings
 * (sleep screen, library browser, Wi-Fi/OPDS, UI theme...) in settings.bin. The enum TYPES these
 * fields are validated against (STATUS_BAR_ITEM, ORIENTATION, PARAGRAPH_ALIGNMENT, REFRESH_FREQUENCY,
 * READER_BUTTON_ACTION, FONT_FAMILY, FONT_SIZE, etc.) stay declared on SystemSetting - BookSetting.h
 * references those extensively as SystemSetting::XXX for per-book override values, and there's no
 * benefit to moving type declarations that carry no persisted state of their own.
 */

#include <cstddef>
#include <cstdint>

#include "state/SystemSetting.h"

class GfxRenderer;

/**
 * @brief Reader/book-reading settings management class
 */
class ReaderSetting {
 private:
  ReaderSetting() = default;
  static ReaderSetting instance;

 public:
  ReaderSetting(const ReaderSetting&) = delete;
  ReaderSetting& operator=(const ReaderSetting&) = delete;

  uint8_t statusBar = 2;  ///< Legacy status bar mode (SystemSetting::STATUS_BAR_MODE::FULL)

  uint8_t statusBarLeft = 6;    ///< Left status bar section (SystemSetting::STATUS_ITEM_BATTERY_ICON_WITH_PERCENT)
  uint8_t statusBarMiddle = 3;  ///< Middle status bar section (SystemSetting::STATUS_ITEM_CHAPTER_TITLE)
  uint8_t statusBarRight = 1;   ///< Right status bar section (SystemSetting::STATUS_ITEM_PAGE_NUMBERS)

  /** Full-width bar - a second, independent bar stacked below the main status bar, spanning
   *  edge-to-edge (ignoring the left/right screen margin the main bar respects). Unlike the main bar
   *  this is NOT a 3-section text bar (that would just duplicate it) - it's a single full-width
   *  loading/progress visualization, so it only ever holds one of the "bar" STATUS_BAR_ITEM values:
   *  NONE, STATUS_ITEM_PROGRESS_BAR, STATUS_ITEM_PROGRESS_BAR_WITH_PERCENT, or STATUS_ITEM_PAGE_BARS
   *  - see StatusBar::kFullBarStyles. Off by default (SystemSetting::STATUS_ITEM_NONE). */
  uint8_t statusBarFullStyle = 0;

  uint8_t extraParagraphSpacing = 1;  ///< Extra paragraph spacing enabled
  uint8_t textAntiAliasing = 0;       ///< New-install default: text anti-aliasing off

  /** Superseded by btnPowerShortAction (see below) - kept only for serialization backward-compat and
   *  as the migration source for it. No longer consulted by the reader. */
  uint8_t readerShortPwrBtn = 1;             ///< SystemSetting::READER_PAGE_REFRESH
  uint8_t xtcShortPwrBtn = 0;                ///< SystemSetting::XTC_POWER_NEXT
  uint8_t xtcPageAutoTurnSeconds = 0;        ///< XTC auto page turn interval, 0=off
  uint8_t xtcImageQuality = 0;               ///< SystemSetting::READER_IMAGE_LOW
  uint8_t xtcRefreshFrequency = 15;          ///< XTC full refresh cadence in pages

  /** Selected /dictionaries/<folder> for EPUB dictionary lookup. Empty = none selected. */
  char dictionaryFolder[64] = "";

  /** Power Button (short-press only, no long-press pair). Supersedes readerShortPwrBtn's
   *  narrower 4-option READER_SHORT_PWRBTN enum; that field stays for serialization backward-compat
   *  (loadFromFile() migrates its value into this field on first load of an older settings file) but is
   *  no longer consulted by the reader. */
  uint8_t btnPowerShortAction = 6;  ///< BTN_ACTION_PAGE_REFRESH

  uint8_t orientation = 0;  ///< SystemSetting::PORTRAIT

  uint8_t fontFamily = SystemSetting::CHAREINK;
  uint8_t fontSize = SystemSetting::SMALL;  ///< 12 pt ChareInk on a fresh install
  uint8_t lineHeight = 100;                 ///< Reader line height, % of natural (10-200)
  uint8_t textSpace = 100;                  ///< Reader word spacing, % of natural (10-200)
  uint8_t paragraphAlignment = 4;           ///< SystemSetting::FOLLOW_CSS
  /** When set, EPUB/CSS `text-indent` is applied (reader "Indent"; passed to Section as respectCssParagraphIndent). */
  uint8_t paragraphCssIndentEnabled = 0;

  uint8_t refreshFrequency = SystemSetting::REFRESH_OFF;  ///< New-install default: off
  uint8_t hyphenationEnabled = 1;    ///< Hyphenation enabled
  uint8_t bionicReadingEnabled = 0;  ///< Bionic Reading enabled
  /** Reading-guide overlay style: 0 = off, 1 = Grid (vertical lines at 1/3 and 2/3 of content width, a
   *  speed-reading aid), 2 = Notebook (horizontal ruled lines like notebook paper). */
  uint8_t readingGuideLinesEnabled = 0;

  uint8_t screenMargin = 0;  ///< Screen margin in pixels

  /**
   * @brief Page auto-turn interval in seconds
   * @details Values: 0 = off, increments of 10 (10, 20, 30, 40, 50, 60)
   */
  uint8_t pageAutoTurnSeconds = 0;

  /** Global Reader image quality: 0=Low(1-bit), 1=Medium(fast 2-bit grayscale),
   *  2=High(quality 2-bit grayscale with text preserved). */
  uint8_t readerImageGrayscale = SystemSetting::READER_IMAGE_LOW;  ///< New-install default: low
  /** When set, image-heavy EPUB pages use a gentler (half) refresh before/after transitions. */
  uint8_t readerSmartRefreshOnImages = 0;

  /** Legacy ignored value retained for settings-file compatibility. */
  uint8_t legacyReaderImagePresentation = 1;
  /** Legacy ignored value retained for settings-file compatibility. */
  uint8_t readerImageDither = 2;  ///< SystemSetting::IMAGE_DITHER_ATKINSON

  /** Long-press on prev/next: 0=off, 1=chapter skip (EPUB), 2=skip 5 pages (EPUB). Legacy files used 0/1 only.
   *  Values match SystemSetting::LONG_PRESS_OFF/LONG_PRESS_CHAPTER_SKIP/LONG_PRESS_PAGE_SKIP_5. */
  uint8_t longPressChapterSkip = 1;  ///< SystemSetting::LONG_PRESS_CHAPTER_SKIP

  /** Bitmask of SystemSetting::READER_BUTTON_ACTION values (bit N = 1u << N) included in the
   *  "Quick Actions" popup opened by a button mapped to BTN_ACTION_QUICK_ACTIONS - see
   *  QuickActionsSettingsActivity (the checklist that edits this) and QuickActionsMenuUi (the popup
   *  that reads it). BTN_ACTION_NONE and BTN_ACTION_QUICK_ACTIONS itself are never set/offered. */
  uint32_t quickActionsMask = 0;  ///< New-install default: all actions off

  /** Daily reading goal in minutes; zero disables the goal. */
  uint8_t dailyReadingGoalMinutes = 5;

  /** Short/long actions for the reader's left/right (or Sticky Up/Down) buttons. */
  uint8_t btnLeftAction = SystemSetting::BTN_ACTION_PAGE_PREVIOUS;
  uint8_t btnRightAction = SystemSetting::BTN_ACTION_PAGE_NEXT;
  uint8_t btnLeftLongAction = SystemSetting::BTN_ACTION_CHAPTER_SKIP_PREVIOUS;
  uint8_t btnRightLongAction = SystemSetting::BTN_ACTION_CHAPTER_SKIP_NEXT;

  /** Reader touch page-turn mode: 0 = swipe, 1 = tap. Tap preserves the existing default behavior. */
  static constexpr uint8_t PAGE_TURN_SWIPE = 0;
  static constexpr uint8_t PAGE_TURN_TAP = 1;
  uint8_t pageTurnMode = PAGE_TURN_TAP;

  /** When set, edge swipes in the reader do not adjust the frontlight. */
  uint8_t disableLightControl = 0;

  /** Reader double-tap action. BTN_ACTION_NONE disables the gesture. */
  uint8_t doubleTapAction = SystemSetting::BTN_ACTION_NONE;

  ~ReaderSetting() = default;

  /**
   * @brief Gets the singleton instance
   * @return Reference to the ReaderSetting instance
   */
  static ReaderSetting& getInstance() { return instance; }

  /**
   * @brief Saves all reader settings to file
   * @return true if save successful, false otherwise
   */
  bool saveToFile() const;

  /**
   * @brief Loads all reader settings from file
   * @return true if load successful, false otherwise
   */
  bool loadFromFile();

  /**
   * @brief Gets reader font ID based on font family and size
   * @return Font identifier for rendering
   */
  int getReaderFontId() const;

  /**
   * @brief Reader font ID for a given family and size (e.g. settings previews).
   */
  int getReaderFontIdForFamilyAndSize(uint8_t family, uint8_t size) const;

  /**
   * @brief Font ID for reader **settings UI** previews only (built-in; avoids loading SD streaming fonts in menus).
   */
  int getReaderFontIdForSettingsUi(uint8_t familySlot, uint8_t sizeIndex) const;

  /**
   * @brief Gets reader line compression factor based on font and spacing
   * @return Line compression multiplier
   */
  float getReaderLineCompression() const;

  /** Word-spacing multiplier (textSpace/100, 100 = natural inter-word space). */
  float getReaderWordSpacingFactor() const;

  /**
   * @brief Gets screen refresh frequency in pages
   * @return Number of pages between refreshes
   */
  int getRefreshFrequency() const;
};

#define READER_SETTINGS ReaderSetting::getInstance()
