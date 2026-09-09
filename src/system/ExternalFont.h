#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

#include "EpdFontData.h"
#include "SDCardManager.h"

class ExternalFont {
 public:
  /** Construct an ExternalFont with no font loaded. */
  ExternalFont();
  /** Unload the font and release its resources. */
  ~ExternalFont();
  /**
   * Load a font from disk. Legacy .bin files are streamed from SD; .ttf/.otf
   * files are copied into PSRAM when available and rasterized on demand.
   */
  bool load(const char* path, bool enableGlyphBitmapCache = true, uint16_t pointSize = 0);
  /** Close the font file and release cached glyph data. */
  void unload();
  /** Drop the persistent PSRAM cache used to reuse TTF/OTF file bytes across loads. */
  static void clearTtfCache();
  /** Enable or disable the shared glyph bitmap cache for this font instance. */
  void setGlyphBitmapCacheEnabled(bool enabled);
  /** Look up glyph metadata for a code point, using the metadata cache when possible. */
  bool getGlyphMetadata(uint32_t codePoint, EpdGlyph& outGlyph);
  /** Read length bytes of glyph bitmap data at offset into outputBuffer, using the bitmap cache when possible. */
  bool getGlyphBitmap(uint32_t offset, uint32_t length, uint8_t* outputBuffer);
  /** Return whether the loaded font contains anti-aliased (grayscale) glyph data. */
  bool hasAntiAliasData() const { return m_hasAntiAliasData; }
  /** Return the underlying EpdFontData for the loaded font. */
  EpdFontData* getData() { return m_fontData; }

 private:
  /** Determine whether the loaded 2-bit font actually contains anti-aliased pixel values. */
  bool detectAntiAliasData();
  /** Read the raw 24-byte glyph table entry at index into out24. */
  bool readGlyphEntryAtIndex(uint32_t index, uint8_t out24[24]) const;
  /** Read the code point stored in the glyph table entry at index. */
  bool readCodepointAtIndex(uint32_t index, uint32_t& outCp) const;
  /** Decode a raw 24-byte glyph table row into an EpdGlyph. */
  void decodeGlyphRow(const uint8_t entry[24], EpdGlyph& out) const;
  /** Look up a glyph's metadata in the per-instance metadata cache. */
  bool metaCacheLookup(uint32_t cp, EpdGlyph& out);
  /** Store a glyph's metadata in the per-instance metadata cache, evicting the oldest entry if full. */
  size_t metaCacheStore(uint32_t cp, const EpdGlyph& g);
  /** Clear the per-instance glyph metadata cache. */
  void metaCacheClear();
  /** Look up cached bitmap data for this font at offset/length, copying it into outputBuffer if found. */
  bool bitmapCacheLookup(uint32_t offset, uint32_t length, uint8_t* outputBuffer);
  /** Store bitmap data for this font in the shared bitmap cache, evicting the oldest entry if full. */
  void bitmapCacheStore(uint32_t offset, uint32_t length, const uint8_t* data);
  /** Clear this font's entries from the shared bitmap cache. */
  void bitmapCacheClear();
  /** Return whether the given offset/length is eligible for bitmap caching. */
  bool bitmapCacheCanStore(uint32_t offset, uint32_t length) const;
  bool loadTtf(const char* path, uint16_t pointSize);
  bool getTtfGlyphMetadata(uint32_t cp, EpdGlyph& out);
  bool getTtfGlyphBitmap(uint32_t offset, uint32_t length, uint8_t* outputBuffer);
  bool ensureTtfBitmap(size_t metaSlot);
  void releaseTtfResources();

  static constexpr uint32_t kTtfBitmapOffsetBase = 0x80000000u;
  static constexpr uint32_t kTtfBitmapOffsetSlotShift = 20;
  static constexpr uint32_t kTtfBitmapOffsetRelativeMask = (1u << kTtfBitmapOffsetSlotShift) - 1u;
  static constexpr size_t kTtfFontInfoBytes = 176;
  static constexpr size_t kTtfSharedCacheSlots = 8;

  struct TtfSharedCacheSlot {
    std::string path;
    uint8_t* data = nullptr;
    uint32_t size = 0;
    uint32_t lastUsed = 0;
    uint8_t users = 0;
    bool inPsram = false;
  };
  static TtfSharedCacheSlot s_ttfSharedCache[kTtfSharedCacheSlots];
  static uint32_t s_ttfSharedCacheGeneration;

  static constexpr size_t kGlyphMetaCacheSlots = 512;
  static constexpr size_t kGlyphBitmapCacheSlots = 128;
  struct GlyphMetaCacheSlot {
    uint32_t cp = 0xFFFFFFFFu;
    uint32_t stamp = 0;
    EpdGlyph glyph{};
  };
  GlyphMetaCacheSlot m_metaCache[kGlyphMetaCacheSlots];
  uint32_t m_metaCacheGen = 0;

  static constexpr size_t kGlyphBitmapCacheMaxBytes = 512;
  struct GlyphBitmapCacheSlot {
    const ExternalFont* owner = nullptr;
    uint32_t offset = 0;
    uint16_t length = 0;
    uint32_t stamp = 0;
    uint8_t data[kGlyphBitmapCacheMaxBytes] = {};
  };
  static GlyphBitmapCacheSlot* s_bitmapCache;
  static uint32_t s_bitmapCacheGen;
  static uint8_t s_bitmapCacheUsers;
  static bool s_bitmapCacheInPsram;
  bool m_bitmapCacheEnabled = false;

  EpdFontData* m_fontData;
  std::string m_filePath;
  FsFile m_file;
  uint32_t m_glyphTableStart = 0;
  uint32_t m_glyphCount = 0;
  uint32_t m_bitmapDataStart = 0;
  bool m_hasAntiAliasData = false;

  bool m_isTtf = false;
  uint8_t* m_ttfData = nullptr;
  uint32_t m_ttfDataSize = 0;
  bool m_ttfDataInPsram = false;
  int8_t m_ttfSharedCacheSlot = -1;
  uint16_t m_ttfPointSize = 0;
  alignas(8) uint8_t m_ttfFontInfo[kTtfFontInfoBytes] = {};
  uint8_t* m_ttfBitmapBuffer = nullptr;
  size_t m_ttfBitmapBufferSize = 0;
  uint32_t m_ttfBitmapToken = 0;
  uint32_t m_ttfBitmapLength = 0;
};
