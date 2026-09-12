#pragma once

/**
 * @file FontManager.h
 * @brief Public interface and types for FontManager.
 */

#include <GfxRenderer.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class EpdFont;
class EpdFontFamily;
class ExternalFont;

class FontManager {
 public:
  struct FontInfo {
    std::string name;
    std::string family;
    int id;
    int size;
    bool isBuiltin;
  };

  /** SD streaming font IDs (must not overlap built-in reader/system font ranges). */
  static constexpr int SD_FONT_START_ID = 5000;

  /** Point-size range exposed for TrueType/OpenType reader fonts. */
  static constexpr uint8_t OUTLINE_FONT_MIN_POINT_SIZE = 8;
  static constexpr uint8_t OUTLINE_FONT_MAX_POINT_SIZE = 60;

  static void initialize(GfxRenderer& renderer);

  static int getNextFont(int currentFontId);

  static int getMaxFontId(int currentFontId);

  static bool scanSDFonts(const char* sdPath = "/fonts", bool forceRescan = false);

  /**
   * Reader "Font Family" slot encoding stored in SystemSetting::fontFamily / BookSettings::fontFamily:
   * 0 = Montserrat, 1+ = SD folder names (sorted),
   * see readerFontFamilyOptionCount().
   */
  static uint32_t readerFontFamilyOptionCount();
  static std::vector<std::string> readerFontFamilyEnumLabels();
  static std::string readerFontFamilyLabel(uint8_t slot);
  static void clampReaderFontFamilySlot(uint8_t& slot);
  static bool isOutlineFontFamily(const std::string& family);
  static bool isOutlineFontFamilySlot(uint8_t slot);
  /** Convert the legacy five-position reader size to its equivalent point size. */
  static int pointSizeForLegacyReaderSize(uint8_t sizeIndex);
  /** Convert an outline point size to the closest legacy five-position size. */
  static uint8_t legacyReaderSizeForPointSize(int pointSize);
  static int getFontIdNearestPointSize(const std::string& family, int preferredPt);
  /** Select the proportional drop-cap size for the active reader font. */
  static int getDropCapFontId(int bodyFontId, uint8_t lineCount);

  static bool loadFontFromSD(int fontId, GfxRenderer& renderer, bool enableGlyphBitmapCache = true);
  static bool ensureFontReady(int fontId, GfxRenderer& renderer);
  /** Find an installed language font that really contains this codepoint. */
  static int findLanguageFontForCodepoint(uint32_t codepoint, int preferredPt, EpdFontFamily::Style style,
                                          GfxRenderer& renderer, const char* preferredLanguageCode = nullptr);
  /** Preload body, next-larger, and max-in-family SD slots used together during EPUB layout. */
  static bool ensureReaderLayoutFonts(int bodyFontId, GfxRenderer& renderer);
  static bool unloadFont(int fontId);
  static void unloadAllSDFonts();

  /** If any SD font is loaded, unload all, run fn, then reload readerBodyFontId when it is an SD slot. */
  static void withSdFontsReleasedForHeapIntensiveWork(int readerBodyFontId, const std::function<void()>& fn);

  static const FontInfo* getFontInfo(int fontId);
  static std::vector<FontInfo> getAllAvailableFonts();
  static std::vector<FontInfo> getFontsByFamily(const std::string& family);
  static std::vector<std::string> getAllFamilies();
  static bool isFontLoaded(int fontId);
  static int getFontId(const std::string& family, int size);

  static void printFontStats();

  static void setMaxLoadedFonts(int maxFonts);
  static int getMaxLoadedFonts();
  static int getLoadedFontCount();
  static void unloadLRUFont();

 private:
  FontManager() = delete;

  struct SDFontEntry {
    int id;
    std::string family;
    int size;
    std::string regularPath;
    std::string boldPath;
    std::string italicPath;
    std::string boldItalicPath;
    EpdFont* regularFont;
    EpdFont* boldFont;
    EpdFont* italicFont;
    EpdFont* boldItalic;
    EpdFontFamily* fontFamily;
    bool isLanguage;
    bool isLoaded;
    uint32_t lastUsed;
  };

  static std::vector<SDFontEntry> g_sdFonts;
  static int g_nextSDFontId;
  static GfxRenderer* g_renderer;
  static std::vector<std::unique_ptr<EpdFontFamily>> g_fontFamilyStorage;
  static std::vector<std::unique_ptr<EpdFont>> g_fontStorage;
  static int g_maxLoadedFonts;
  static int g_loadedFontCount;
  static bool g_scannedForFonts;

  static void updateFontLRU(int fontId);
  static void cleanupFontData(SDFontEntry* entry);
  static void rebuildSdReaderFamilyList();
};
