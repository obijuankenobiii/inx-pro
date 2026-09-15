#include "ExternalFont.h"

#include <Arduino.h>
#include <Utf8.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <new>

#include <esp_heap_caps.h>

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "../../freeink-sdk/libs/book/FreeInkBook/third_party/stb/stb_truetype.h"

/** Construct an ExternalFont with no font loaded. */
ExternalFont::ExternalFont() : m_fontData(nullptr) { allocateMetaCache(); }

/** Unload the font and release its resources. */
ExternalFont::~ExternalFont() {
  unload();
  releaseMetaCache();
}

ExternalFont::GlyphBitmapCacheSlot* ExternalFont::s_bitmapCache = nullptr;
uint32_t ExternalFont::s_bitmapCacheGen = 0;
uint8_t ExternalFont::s_bitmapCacheUsers = 0;
bool ExternalFont::s_bitmapCacheInPsram = false;
ExternalFont::TtfSharedCacheSlot ExternalFont::s_ttfSharedCache[ExternalFont::kTtfSharedCacheSlots];
uint32_t ExternalFont::s_ttfSharedCacheGeneration = 0;

namespace {

constexpr uint32_t kPackedFontMagic = 0x45504446u;
constexpr uint32_t kPackedFontVersion = 1u;

uint8_t* allocateFontBuffer(const size_t bytes, bool* inPsram) {
  if (inPsram) *inPsram = false;
#if defined(ARDUINO_ARCH_ESP32)
  if (uint8_t* psram = static_cast<uint8_t*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT))) {
    if (inPsram) *inPsram = true;
    return psram;
  }
  return static_cast<uint8_t*>(heap_caps_malloc(bytes, MALLOC_CAP_8BIT));
#else
  return static_cast<uint8_t*>(std::malloc(bytes));
#endif
}

void releaseFontBuffer(void* buffer) {
  if (!buffer) return;
#if defined(ARDUINO_ARCH_ESP32)
  heap_caps_free(buffer);
#else
  std::free(buffer);
#endif
}

bool pathHasExtension(const char* path, const char* extension) {
  if (!path || !extension) return false;
  const char* dot = strrchr(path, '.');
  if (!dot) return false;
  while (*dot && *extension) {
    char a = *dot++;
    char b = *extension++;
    if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
    if (a != b) return false;
  }
  return *dot == '\0' && *extension == '\0';
}

int roundedMetric(const float value) {
  return static_cast<int>(value + (value < 0.0f ? -0.5f : 0.5f));
}

// Reader font sizes are point sizes. Keep the TTF path consistent with the
// legacy FreeType converter, which renders at 150 DPI (pt * 150 / 72 px/em).
constexpr float kReaderFontDpi = 150.0f;

float ttfScaleForPointSize(const stbtt_fontinfo* font, const uint16_t pointSize) {
  const float pixelsPerEm = static_cast<float>(pointSize) * kReaderFontDpi / 72.0f;
  return stbtt_ScaleForMappingEmToPixels(font, pixelsPerEm);
}

}  // namespace

/** Clear the per-instance glyph metadata cache. */
void ExternalFont::metaCacheClear() {
  if (!m_metaCache) return;
  for (size_t i = 0; i < kGlyphMetaCacheSlots; ++i) {
    m_metaCache[i].cp = 0xFFFFFFFFu;
    m_metaCache[i].stamp = 0;
    m_metaCache[i].glyph = EpdGlyph{};
  }
  m_metaCacheGen = 0;
}

void ExternalFont::allocateMetaCache() {
#if defined(ARDUINO_ARCH_ESP32)
  m_metaCache = static_cast<GlyphMetaCacheSlot*>(
      heap_caps_calloc(kGlyphMetaCacheSlots, sizeof(GlyphMetaCacheSlot), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  m_metaCacheInPsram = m_metaCache != nullptr;
#endif
  if (!m_metaCache) {
    m_metaCache = new (std::nothrow) GlyphMetaCacheSlot[kGlyphMetaCacheSlots]();
    m_metaCacheInPsram = false;
  }
  metaCacheClear();
}

void ExternalFont::releaseMetaCache() {
  if (!m_metaCache) return;
#if defined(ARDUINO_ARCH_ESP32)
  if (m_metaCacheInPsram) {
    heap_caps_free(m_metaCache);
  } else {
    delete[] m_metaCache;
  }
#else
  delete[] m_metaCache;
#endif
  m_metaCache = nullptr;
  m_metaCacheInPsram = false;
}

/** Clear this font's entries from the shared bitmap cache. */
void ExternalFont::bitmapCacheClear() {
  if (s_bitmapCache) {
    for (size_t i = 0; i < kGlyphBitmapCacheSlots; ++i) {
      if (s_bitmapCache[i].owner == this) {
        s_bitmapCache[i].owner = nullptr;
        s_bitmapCache[i].offset = 0;
        s_bitmapCache[i].length = 0;
        s_bitmapCache[i].stamp = 0;
      }
    }
  }
}

/** Enable or disable the shared glyph bitmap cache for this font instance. */
void ExternalFont::setGlyphBitmapCacheEnabled(const bool enabled) {
  if (enabled == m_bitmapCacheEnabled) {
    return;
  }

  m_bitmapCacheEnabled = enabled;
  if (!enabled) {
    bitmapCacheClear();
    if (s_bitmapCacheUsers > 0) {
      --s_bitmapCacheUsers;
    }
    if (s_bitmapCacheUsers == 0) {
      if (s_bitmapCacheInPsram) {
        heap_caps_free(s_bitmapCache);
      } else {
        delete[] s_bitmapCache;
      }
      s_bitmapCacheInPsram = false;
      s_bitmapCache = nullptr;
      s_bitmapCacheGen = 0;
    }
    return;
  }

  if (!s_bitmapCache) {
    s_bitmapCache = static_cast<GlyphBitmapCacheSlot*>(
        heap_caps_calloc(kGlyphBitmapCacheSlots, sizeof(GlyphBitmapCacheSlot), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (s_bitmapCache) {
      s_bitmapCacheInPsram = true;
    } else {
      s_bitmapCache = new (std::nothrow) GlyphBitmapCacheSlot[kGlyphBitmapCacheSlots]();
      s_bitmapCacheInPsram = false;
    }
    s_bitmapCacheGen = 0;
  }
  if (s_bitmapCache) {
    ++s_bitmapCacheUsers;
    bitmapCacheClear();
  } else {
    m_bitmapCacheEnabled = false;
  }
}

/** Close the font file and release cached glyph data. */
void ExternalFont::unload() {
  setGlyphBitmapCacheEnabled(false);
  if (m_file) m_file.close();
  releaseTtfResources();
  if (m_fontData) {
    delete m_fontData;
    m_fontData = nullptr;
  }
  m_glyphTableStart = 0;
  m_glyphCount = 0;
  m_bitmapDataStart = 0;
  m_fileSize = 0;
  m_hasAntiAliasData = false;
  metaCacheClear();
}

void ExternalFont::clearTtfCache() {
  for (auto& slot : s_ttfSharedCache) {
    if (slot.users != 0) {
      continue;
    }
    releaseFontBuffer(slot.data);
    slot = TtfSharedCacheSlot{};
  }
}

/** Load font metadata from an on-disk font file at path, keeping glyph data on SD for on-demand reads. */
bool ExternalFont::load(const char* path, const bool enableGlyphBitmapCache, const uint16_t pointSize) {
  unload();
  if (pathHasExtension(path, ".ttf") || pathHasExtension(path, ".otf")) {
    const bool loaded = loadTtf(path, pointSize);
    if (loaded) setGlyphBitmapCacheEnabled(enableGlyphBitmapCache);
    return loaded;
  }

  m_filePath = path;
  m_file = SdMan.open(path, FILE_READ);
  if (!m_file) return false;

  const uint64_t fileSize = m_file.size();
  if (fileSize == 0 || fileSize > UINT32_MAX) {
    INX_SERIAL.printf("[ExternalFont] Invalid packed font size: %s\n", path);
    return false;
  }
  m_fileSize = static_cast<uint32_t>(fileSize);

  const auto readExact = [this](void* destination, const size_t length) {
    return m_file.read(static_cast<uint8_t*>(destination), length) == length;
  };

  uint32_t magic, version;
  if (!readExact(&magic, sizeof(magic)) || !readExact(&version, sizeof(version)) || magic != kPackedFontMagic ||
      version != kPackedFontVersion) {
    INX_SERIAL.printf("[ExternalFont] Invalid packed font header: %s\n", path);
    return false;
  }

  uint16_t nameLen;
  if (!readExact(&nameLen, sizeof(nameLen)) || nameLen > m_fileSize - m_file.position() ||
      !m_file.seek(m_file.position() + nameLen)) {
    INX_SERIAL.printf("[ExternalFont] Invalid packed font name: %s\n", path);
    return false;
  }

  m_fontData = new (std::nothrow) EpdFontData();
  if (!m_fontData) return false;
  int16_t lineHeight, ascender, descender;
  if (!readExact(&lineHeight, sizeof(lineHeight)) || !readExact(&ascender, sizeof(ascender)) ||
      !readExact(&descender, sizeof(descender))) {
    INX_SERIAL.printf("[ExternalFont] Truncated packed font metrics: %s\n", path);
    return false;
  }
  uint8_t is2Bit;
  if (!readExact(&is2Bit, sizeof(is2Bit))) {
    INX_SERIAL.printf("[ExternalFont] Truncated packed font format: %s\n", path);
    return false;
  }

  int lh = static_cast<int>(lineHeight);
  if (lh < 1) lh = 12;
  if (lh > 255) lh = 255;
  m_fontData->advanceY = static_cast<uint8_t>(lh);
  m_fontData->ascender = ascender;
  m_fontData->descender = descender;
  m_fontData->is2Bit = (is2Bit != 0);
  m_fontData->bitmap = nullptr;
  m_fontData->bitmapSize = 0;
  m_fontData->glyph = nullptr;
  m_fontData->intervals = nullptr;
  m_fontData->intervalCount = 0;

  uint16_t intervalCount;
  if (!readExact(&intervalCount, sizeof(intervalCount)) ||
      static_cast<uint64_t>(intervalCount) * 12u > m_fileSize - m_file.position() ||
      !m_file.seek(m_file.position() + (intervalCount * 12u))) {
    INX_SERIAL.printf("[ExternalFont] Invalid packed font intervals: %s\n", path);
    return false;
  }

  if (!readExact(&m_glyphCount, sizeof(m_glyphCount))) {
    INX_SERIAL.printf("[ExternalFont] Missing packed font glyph count: %s\n", path);
    return false;
  }
  m_glyphTableStart = m_file.position();

  const uint64_t tableBytes = static_cast<uint64_t>(m_glyphCount) * 24u;
  if (tableBytes > m_fileSize - m_glyphTableStart ||
      !m_file.seek(m_glyphTableStart + static_cast<uint32_t>(tableBytes))) {
    INX_SERIAL.printf("[ExternalFont] Invalid packed font glyph table: %s\n", path);
    return false;
  }
  m_bitmapDataStart = m_file.position();
  setGlyphBitmapCacheEnabled(enableGlyphBitmapCache);
  m_hasAntiAliasData = enableGlyphBitmapCache && m_fontData->is2Bit && detectAntiAliasData();

  metaCacheClear();
  bitmapCacheClear();
  if (m_bitmapCacheEnabled) {
    INX_SERIAL.printf(
        "[ExternalFont] On-demand glyph table: %u glyphs, mode=%s, aa=%d, meta %u-slot (~%u B, %s), bitmap %u-slot x "
        "%u B (~%u B)\n",
        m_glyphCount, m_fontData->is2Bit ? "2bit" : "1bit", m_hasAntiAliasData ? 1 : 0,
        static_cast<unsigned>(kGlyphMetaCacheSlots),
        static_cast<unsigned>(kGlyphMetaCacheSlots * sizeof(GlyphMetaCacheSlot)),
        m_metaCacheInPsram ? "PSRAM" : "internal",
        static_cast<unsigned>(kGlyphBitmapCacheSlots), static_cast<unsigned>(kGlyphBitmapCacheMaxBytes),
        static_cast<unsigned>(kGlyphBitmapCacheSlots * sizeof(GlyphBitmapCacheSlot)));
  } else {
    INX_SERIAL.printf(
        "[ExternalFont] On-demand glyph table: %u glyphs, mode=%s, aa=0, meta %u-slot (~%u B, %s), bitmap cache off\n",
        m_glyphCount, m_fontData->is2Bit ? "2bit" : "1bit", static_cast<unsigned>(kGlyphMetaCacheSlots),
        static_cast<unsigned>(kGlyphMetaCacheSlots * sizeof(GlyphMetaCacheSlot)),
        m_metaCacheInPsram ? "PSRAM" : "internal");
  }
  return true;
}

/** Load a TrueType/OpenType file into PSRAM and initialize stb_truetype over its bytes. */
bool ExternalFont::loadTtf(const char* path, const uint16_t pointSize) {
  if (!path || pointSize == 0) {
    INX_SERIAL.println("[ExternalFont] TTF/OTF load requires a path and point size");
    return false;
  }

  m_filePath = path;

  for (size_t i = 0; i < kTtfSharedCacheSlots; ++i) {
    auto& slot = s_ttfSharedCache[i];
    if (slot.data != nullptr && slot.path == path) {
      slot.users++;
      slot.lastUsed = ++s_ttfSharedCacheGeneration;
      m_ttfData = slot.data;
      m_ttfDataSize = slot.size;
      m_ttfDataInPsram = slot.inPsram;
      m_ttfSharedCacheSlot = static_cast<int8_t>(i);
      INX_SERIAL.printf("[ExternalFont] Reused cached TTF %s (%lu bytes, %upt)\n", path,
                        static_cast<unsigned long>(m_ttfDataSize), static_cast<unsigned>(pointSize));
      break;
    }
  }

  if (!m_ttfData) {
    m_file = SdMan.open(path, FILE_READ);
    if (!m_file) {
      INX_SERIAL.printf("[ExternalFont] Could not open TTF/OTF: %s\n", path);
      return false;
    }

    const uint64_t fileSize = m_file.size();
    if (fileSize == 0 || fileSize > UINT32_MAX) {
      INX_SERIAL.printf("[ExternalFont] Invalid TTF/OTF size: %llu\n", static_cast<unsigned long long>(fileSize));
      m_file.close();
      return false;
    }

    m_ttfDataSize = static_cast<uint32_t>(fileSize);
    m_ttfData = allocateFontBuffer(m_ttfDataSize, &m_ttfDataInPsram);
    if (!m_ttfData) {
      INX_SERIAL.printf("[ExternalFont] Could not allocate %lu bytes for TTF/OTF %s\n",
                        static_cast<unsigned long>(m_ttfDataSize), path);
      m_file.close();
      m_ttfDataSize = 0;
      return false;
    }

    uint32_t totalRead = 0;
    while (totalRead < m_ttfDataSize) {
      const uint32_t remaining = m_ttfDataSize - totalRead;
      const uint32_t chunk = std::min<uint32_t>(remaining, 16u * 1024u);
      const size_t bytesRead = m_file.read(m_ttfData + totalRead, chunk);
      if (bytesRead != chunk) {
        INX_SERIAL.printf("[ExternalFont] TTF/OTF read mismatch for %s: expected %lu, got %u\n", path,
                          static_cast<unsigned long>(chunk), static_cast<unsigned>(bytesRead));
        releaseTtfResources();
        m_file.close();
        return false;
      }
      totalRead += chunk;
    }
    m_file.close();

    int cacheSlot = -1;
    uint32_t oldest = UINT32_MAX;
    for (size_t i = 0; i < kTtfSharedCacheSlots; ++i) {
      auto& slot = s_ttfSharedCache[i];
      if (slot.users == 0 && (slot.data == nullptr || slot.lastUsed < oldest)) {
        oldest = slot.lastUsed;
        cacheSlot = static_cast<int>(i);
      }
    }
    if (cacheSlot >= 0) {
      auto& slot = s_ttfSharedCache[cacheSlot];
      releaseFontBuffer(slot.data);
      slot.path = path;
      slot.data = m_ttfData;
      slot.size = m_ttfDataSize;
      slot.lastUsed = ++s_ttfSharedCacheGeneration;
      slot.users = 1;
      slot.inPsram = m_ttfDataInPsram;
      m_ttfSharedCacheSlot = static_cast<int8_t>(cacheSlot);
    }
  }

  static_assert(sizeof(stbtt_fontinfo) <= kTtfFontInfoBytes, "TTF font info storage is too small");
  const int fontOffset = stbtt_GetFontOffsetForIndex(m_ttfData, 0);
  if (fontOffset < 0 || stbtt_InitFont(reinterpret_cast<stbtt_fontinfo*>(m_ttfFontInfo), m_ttfData, fontOffset) == 0) {
    INX_SERIAL.printf("[ExternalFont] Invalid TTF/OTF data: %s\n", path);
    releaseTtfResources();
    return false;
  }

  int unitsAscent = 0;
  int unitsDescent = 0;
  int unitsLineGap = 0;
  stbtt_GetFontVMetrics(reinterpret_cast<stbtt_fontinfo*>(m_ttfFontInfo), &unitsAscent, &unitsDescent, &unitsLineGap);
  const float scale = ttfScaleForPointSize(reinterpret_cast<stbtt_fontinfo*>(m_ttfFontInfo), pointSize);
  int lineHeight = roundedMetric((unitsAscent - unitsDescent + unitsLineGap) * scale);
  if (lineHeight < 1) lineHeight = pointSize;
  lineHeight = std::min(lineHeight, 255);

  m_fontData = new (std::nothrow) EpdFontData();
  if (!m_fontData) {
    releaseTtfResources();
    return false;
  }
  m_fontData->bitmap = nullptr;
  m_fontData->bitmapSize = 0;
  m_fontData->glyph = nullptr;
  m_fontData->intervals = nullptr;
  m_fontData->intervalCount = 0;
  m_fontData->advanceY = static_cast<uint8_t>(lineHeight);
  m_fontData->ascender = roundedMetric(unitsAscent * scale);
  m_fontData->descender = roundedMetric(unitsDescent * scale);
  m_fontData->is2Bit = true;

  m_isTtf = true;
  m_ttfPointSize = pointSize;
  m_hasAntiAliasData = true;
  metaCacheClear();
  INX_SERIAL.printf("[ExternalFont] Loaded TTF %s (%lu bytes, %upt, PSRAM=%d)\n", path,
                    static_cast<unsigned long>(m_ttfDataSize), static_cast<unsigned>(pointSize),
                    m_ttfDataInPsram ? 1 : 0);
  return true;
}

/** Release the in-memory TrueType file and reusable glyph bitmap buffer. */
void ExternalFont::releaseTtfResources() {
  if (m_ttfSharedCacheSlot >= 0 && m_ttfSharedCacheSlot < static_cast<int8_t>(kTtfSharedCacheSlots)) {
    auto& slot = s_ttfSharedCache[static_cast<size_t>(m_ttfSharedCacheSlot)];
    if (slot.users > 0) {
      --slot.users;
    }
  } else {
    releaseFontBuffer(m_ttfData);
  }
  releaseFontBuffer(m_ttfBitmapBuffer);
  m_ttfData = nullptr;
  m_ttfBitmapBuffer = nullptr;
  m_ttfDataSize = 0;
  m_ttfBitmapBufferSize = 0;
  m_ttfDataInPsram = false;
  m_ttfSharedCacheSlot = -1;
  m_ttfPointSize = 0;
  m_ttfBitmapToken = 0;
  m_ttfBitmapLength = 0;
  m_isTtf = false;
}

/** Determine whether the loaded 2-bit font actually contains anti-aliased pixel values. */
bool ExternalFont::detectAntiAliasData() {
  if (!m_fontData || !m_fontData->is2Bit || m_glyphCount == 0) {
    return false;
  }

  static constexpr uint32_t kSampleCodepoints[] = {
      'a', 'e', 'g', 'm', 'n', 'o', 's', 't', 'A', 'E', 'H', 'M', 'O', 'S', '0', '2', '8',
  };
  uint8_t buffer[256];

  auto glyphHasGray = [&](const EpdGlyph& glyph) -> bool {
    if (glyph.dataLength == 0) {
      return false;
    }

    uint32_t remaining = glyph.dataLength;
    uint32_t offset = glyph.dataOffset;
    while (remaining > 0) {
      const uint32_t chunk = remaining > sizeof(buffer) ? sizeof(buffer) : remaining;
      if (!getGlyphBitmap(offset, chunk, buffer)) {
        return false;
      }
      for (uint32_t i = 0; i < chunk; ++i) {
        const uint8_t byte = buffer[i];
        for (uint8_t shift = 0; shift < 8; shift += 2) {
          const uint8_t value = (byte >> shift) & 0x03u;
          if (value == 1u || value == 2u) {
            return true;
          }
        }
      }
      offset += chunk;
      remaining -= chunk;
    }
    return false;
  };

  const bool sampleHasGray = std::any_of(std::begin(kSampleCodepoints), std::end(kSampleCodepoints), [&](uint32_t cp) {
    EpdGlyph glyph{};
    return getGlyphMetadata(cp, glyph) && glyphHasGray(glyph);
  });
  if (sampleHasGray) {
    return true;
  }

  static constexpr uint32_t kMaxFallbackGlyphs = 96;
  const uint32_t limit = m_glyphCount < kMaxFallbackGlyphs ? m_glyphCount : kMaxFallbackGlyphs;
  for (uint32_t i = 0; i < limit; ++i) {
    uint8_t entry[24];
    EpdGlyph glyph{};
    if (!readGlyphEntryAtIndex(i, entry)) {
      return false;
    }
    decodeGlyphRow(entry, glyph);
    if (glyphHasGray(glyph)) {
      return true;
    }
  }
  return false;
}

/** Read the raw 24-byte glyph table entry at index into out24. */
bool ExternalFont::readGlyphEntryAtIndex(uint32_t index, uint8_t out24[24]) const {
  if (index >= m_glyphCount || out24 == nullptr) {
    return false;
  }
  FsFile* useFile = const_cast<FsFile*>(&m_file);
  if (!*useFile || !useFile->isOpen()) {
    *useFile = SdMan.open(m_filePath.c_str(), FILE_READ);
    if (!*useFile) {
      return false;
    }
  }
  const uint32_t pos = m_glyphTableStart + index * 24u;
  if (!useFile->seek(pos)) {
    return false;
  }
  return useFile->read(out24, 24) == 24;
}

/** Read the code point stored in the glyph table entry at index. */
bool ExternalFont::readCodepointAtIndex(uint32_t index, uint32_t& outCp) const {
  if (index >= m_glyphCount) {
    return false;
  }
  FsFile* useFile = const_cast<FsFile*>(&m_file);
  if (!*useFile || !useFile->isOpen()) {
    *useFile = SdMan.open(m_filePath.c_str(), FILE_READ);
    if (!*useFile) {
      return false;
    }
  }
  const uint32_t pos = m_glyphTableStart + index * 24u + 18u;
  if (!useFile->seek(pos)) {
    return false;
  }
  return useFile->read(reinterpret_cast<uint8_t*>(&outCp), 4) == 4;
}

/** Decode a raw 24-byte glyph table row into an EpdGlyph. */
void ExternalFont::decodeGlyphRow(const uint8_t entry[24], EpdGlyph& out) const {
  uint16_t w = 0, h = 0, ax = 0;
  int16_t lef = 0, tp = 0;
  uint32_t dlen = 0, rel = 0;
  memcpy(&w, entry + 0, 2);
  memcpy(&h, entry + 2, 2);
  memcpy(&ax, entry + 4, 2);
  memcpy(&lef, entry + 6, 2);
  memcpy(&tp, entry + 8, 2);
  memcpy(&dlen, entry + 10, 4);
  memcpy(&rel, entry + 14, 4);
  out.width = static_cast<uint8_t>(w > 255 ? 255 : w);
  out.height = static_cast<uint8_t>(h > 255 ? 255 : h);
  out.advanceX = static_cast<uint8_t>(ax > 255 ? 255 : ax);
  out.left = lef;
  out.top = tp;
  out.dataLength = static_cast<uint16_t>(dlen > 0xFFFFu ? 0xFFFFu : dlen);
  out.dataOffset = m_bitmapDataStart + rel;
}

/** Look up a glyph's metadata in the per-instance metadata cache. */
bool ExternalFont::metaCacheLookup(uint32_t cp, EpdGlyph& out) {
  if (!m_metaCache) return false;
  for (size_t i = 0; i < kGlyphMetaCacheSlots; ++i) {
    if (m_metaCache[i].cp == cp) {
      out = m_metaCache[i].glyph;
      m_metaCache[i].stamp = ++m_metaCacheGen;
      return true;
    }
  }
  return false;
}

size_t ExternalFont::metaCacheStore(uint32_t cp, const EpdGlyph& g) {
  if (!m_metaCache) return 0;
  size_t slot = 0;
  uint32_t bestStamp = 0xFFFFFFFFu;
  for (size_t i = 0; i < kGlyphMetaCacheSlots; ++i) {
    if (m_metaCache[i].cp == 0xFFFFFFFFu) {
      slot = i;
      break;
    }
    if (m_metaCache[i].stamp < bestStamp) {
      bestStamp = m_metaCache[i].stamp;
      slot = i;
    }
  }
  if (m_isTtf && m_metaCache[slot].cp != 0xFFFFFFFFu) {
    // TTF bitmap offsets encode the metadata-cache slot. Invalidate both
    // caches before reusing a slot so an evicted glyph cannot alias its
    // predecessor's rasterized bitmap.
    m_ttfBitmapToken = 0;
    bitmapCacheClear();
  }
  m_metaCache[slot].cp = cp;
  m_metaCache[slot].glyph = g;
  m_metaCache[slot].stamp = ++m_metaCacheGen;
  return slot;
}

bool ExternalFont::bitmapCacheLookup(uint32_t offset, uint32_t length, uint8_t* outputBuffer) {
  if (!s_bitmapCache || !outputBuffer || !bitmapCacheCanStore(offset, length)) {
    return false;
  }
  for (size_t i = 0; i < kGlyphBitmapCacheSlots; ++i) {
    if (s_bitmapCache[i].owner == this && s_bitmapCache[i].length == length && s_bitmapCache[i].offset == offset) {
      memcpy(outputBuffer, s_bitmapCache[i].data, length);
      s_bitmapCache[i].stamp = ++s_bitmapCacheGen;
      return true;
    }
  }
  return false;
}

void ExternalFont::bitmapCacheStore(uint32_t offset, uint32_t length, const uint8_t* data) {
  if (!s_bitmapCache || !data || !bitmapCacheCanStore(offset, length)) {
    return;
  }
  size_t slot = 0;
  uint32_t bestStamp = 0xFFFFFFFFu;
  for (size_t i = 0; i < kGlyphBitmapCacheSlots; ++i) {
    if (s_bitmapCache[i].length == 0) {
      slot = i;
      break;
    }
    if (s_bitmapCache[i].stamp < bestStamp) {
      bestStamp = s_bitmapCache[i].stamp;
      slot = i;
    }
  }
  s_bitmapCache[slot].owner = this;
  s_bitmapCache[slot].offset = offset;
  s_bitmapCache[slot].length = static_cast<uint16_t>(length);
  memcpy(s_bitmapCache[slot].data, data, length);
  s_bitmapCache[slot].stamp = ++s_bitmapCacheGen;
}

bool ExternalFont::bitmapCacheCanStore(const uint32_t offset, const uint32_t length) const {
  if (!m_bitmapCacheEnabled || !s_bitmapCache || offset == 0 || length == 0 || length > kGlyphBitmapCacheMaxBytes) {
    return false;
  }
  return true;
}

bool ExternalFont::getGlyphMetadata(uint32_t cp, EpdGlyph& out) {
  if (m_isTtf) {
    return getTtfGlyphMetadata(cp, out);
  }

  if (m_glyphCount == 0) {
    return false;
  }
  if (!m_metaCache) return false;

  if (metaCacheLookup(cp, out)) {
    return true;
  }

  int32_t low = 0;
  int32_t high = static_cast<int32_t>(m_glyphCount) - 1;
  int32_t found = -1;

  while (low <= high) {
    const int32_t mid = low + (high - low) / 2;
    uint32_t midCp = 0;
    if (!readCodepointAtIndex(static_cast<uint32_t>(mid), midCp)) {
      return false;
    }

    if (midCp == cp) {
      found = mid;
      break;
    }

    if (midCp < cp) {
      low = mid + 1;
    } else {
      high = mid - 1;
    }
  }

  if (found < 0) {
    return false;
  }

  uint8_t entry[24];
  if (!readGlyphEntryAtIndex(static_cast<uint32_t>(found), entry)) {
    return false;
  }
  decodeGlyphRow(entry, out);
  const uint64_t pixelCount = static_cast<uint64_t>(out.width) * out.height;
  const uint64_t requiredBytes = m_fontData->is2Bit ? (pixelCount + 3u) / 4u : (pixelCount + 7u) / 8u;
  if (out.dataOffset > m_fileSize || out.dataLength < requiredBytes ||
      out.dataLength > m_fileSize - out.dataOffset) {
    return false;
  }
  metaCacheStore(cp, out);
  return true;
}

/** Build EpdGlyph metadata from the TrueType/OpenType face at the configured pixel size. */
bool ExternalFont::getTtfGlyphMetadata(const uint32_t cp, EpdGlyph& out) {
  if (!m_isTtf || !m_fontData || !m_ttfData) return false;
  if (!m_metaCache) return false;
  if (metaCacheLookup(cp, out)) return true;

  auto* font = reinterpret_cast<stbtt_fontinfo*>(m_ttfFontInfo);
  const int glyphIndex = stbtt_FindGlyphIndex(font, static_cast<int>(cp));
  if (glyphIndex == 0) return false;

  const float scale = ttfScaleForPointSize(font, m_ttfPointSize);
  int x0 = 0;
  int y0 = 0;
  int x1 = 0;
  int y1 = 0;
  stbtt_GetGlyphBitmapBox(font, glyphIndex, scale, scale, &x0, &y0, &x1, &y1);
  const int width = x1 - x0;
  const int height = y1 - y0;
  if (width < 0 || height < 0 || width > 255 || height > 255) return false;

  const uint64_t pixelCount = static_cast<uint64_t>(width) * static_cast<uint64_t>(height);
  const uint64_t packedLength = (pixelCount + 3u) / 4u;
  if (packedLength > UINT16_MAX) return false;

  int advance = 0;
  int leftSideBearing = 0;
  stbtt_GetGlyphHMetrics(font, glyphIndex, &advance, &leftSideBearing);
  (void)leftSideBearing;

  EpdGlyph glyph{};
  glyph.width = static_cast<uint8_t>(width);
  glyph.height = static_cast<uint8_t>(height);
  glyph.advanceX = static_cast<uint8_t>(std::clamp(roundedMetric(advance * scale), 0, 255));
  glyph.left = static_cast<int16_t>(std::clamp(x0, static_cast<int>(INT16_MIN), static_cast<int>(INT16_MAX)));
  glyph.top = static_cast<int16_t>(std::clamp(-y0, static_cast<int>(INT16_MIN), static_cast<int>(INT16_MAX)));
  glyph.dataLength = static_cast<uint16_t>(packedLength);

  const size_t slot = metaCacheStore(cp, glyph);
  m_metaCache[slot].glyph.dataOffset = kTtfBitmapOffsetBase |
                                        (static_cast<uint32_t>(slot) << kTtfBitmapOffsetSlotShift);
  out = m_metaCache[slot].glyph;
  return true;
}

/** Rasterize and pack one TrueType/OpenType glyph into the reusable per-font buffer. */
bool ExternalFont::ensureTtfBitmap(const size_t metaSlot) {
  if (!m_isTtf || metaSlot >= kGlyphMetaCacheSlots || m_metaCache[metaSlot].cp == 0xFFFFFFFFu) return false;
  const EpdGlyph& glyph = m_metaCache[metaSlot].glyph;
  const uint32_t token = kTtfBitmapOffsetBase |
                         (static_cast<uint32_t>(metaSlot) << kTtfBitmapOffsetSlotShift);
  if (glyph.dataLength == 0) {
    m_ttfBitmapToken = token;
    m_ttfBitmapLength = 0;
    return true;
  }
  if (m_ttfBitmapToken == token && m_ttfBitmapBuffer && m_ttfBitmapLength >= glyph.dataLength) return true;

  const size_t pixelCount = static_cast<size_t>(glyph.width) * static_cast<size_t>(glyph.height);
  if (!m_ttfBitmapBuffer || m_ttfBitmapBufferSize < pixelCount) {
    releaseFontBuffer(m_ttfBitmapBuffer);
    m_ttfBitmapBuffer = allocateFontBuffer(pixelCount, nullptr);
    m_ttfBitmapBufferSize = m_ttfBitmapBuffer ? pixelCount : 0;
  }
  if (!m_ttfBitmapBuffer) return false;

  auto* font = reinterpret_cast<stbtt_fontinfo*>(m_ttfFontInfo);
  const int glyphIndex = stbtt_FindGlyphIndex(font, static_cast<int>(m_metaCache[metaSlot].cp));
  if (glyphIndex == 0) return false;
  const float scale = ttfScaleForPointSize(font, m_ttfPointSize);
  stbtt_MakeGlyphBitmap(font, m_ttfBitmapBuffer, glyph.width, glyph.height, glyph.width, scale, scale, glyphIndex);

  for (size_t pixel = 0; pixel < pixelCount; pixel += 4) {
    uint8_t packed = 0;
    const size_t count = std::min<size_t>(4, pixelCount - pixel);
    for (size_t i = 0; i < count; ++i) {
      const uint8_t coverage = m_ttfBitmapBuffer[pixel + i];
      const uint8_t level = coverage >= 192 ? 3 : (coverage >= 128 ? 2 : (coverage >= 64 ? 1 : 0));
      packed = static_cast<uint8_t>((packed << 2) | level);
    }
    if (count < 4) packed = static_cast<uint8_t>(packed << ((4 - count) * 2));
    m_ttfBitmapBuffer[pixel / 4] = packed;
  }
  m_ttfBitmapToken = token;
  m_ttfBitmapLength = glyph.dataLength;
  return true;
}

/** Read a packed TrueType/OpenType glyph bitmap, supporting row-by-row reads by offset. */
bool ExternalFont::getTtfGlyphBitmap(const uint32_t offset, const uint32_t length, uint8_t* outputBuffer) {
  if (!outputBuffer || (offset & kTtfBitmapOffsetBase) == 0) return false;
  const size_t metaSlot = (offset >> kTtfBitmapOffsetSlotShift) & 0x7FFu;
  const uint32_t relativeOffset = offset & kTtfBitmapOffsetRelativeMask;
  if (metaSlot >= kGlyphMetaCacheSlots || m_metaCache[metaSlot].cp == 0xFFFFFFFFu) return false;
  const EpdGlyph& glyph = m_metaCache[metaSlot].glyph;
  if (relativeOffset > glyph.dataLength || length > glyph.dataLength - relativeOffset) return false;
  if (!ensureTtfBitmap(metaSlot)) return false;
  if (length != 0) memcpy(outputBuffer, m_ttfBitmapBuffer + relativeOffset, length);
  return true;
}

bool ExternalFont::getGlyphBitmap(uint32_t absoluteOffset, uint32_t length, uint8_t* buffer) {
  if (length == 0) return true;
  if (!buffer) return false;

  if (!m_isTtf && (absoluteOffset > m_fileSize || length > m_fileSize - absoluteOffset)) {
    INX_SERIAL.printf("[ExternalFont] ERR: Glyph bitmap outside file bounds (%u + %u > %u)\n", absoluteOffset, length,
                      m_fileSize);
    return false;
  }

  if (bitmapCacheLookup(absoluteOffset, length, buffer)) {
    return true;
  }

  if (m_isTtf) {
    if (!getTtfGlyphBitmap(absoluteOffset, length, buffer)) return false;
    bitmapCacheStore(absoluteOffset, length, buffer);
    return true;
  }

  if (!m_file || !m_file.isOpen()) {
    m_file = SdMan.open(m_filePath.c_str(), FILE_READ);
    if (!m_file) {
      INX_SERIAL.printf("[ExternalFont] ERR: Could not reopen %s for bitmap read\n", m_filePath.c_str());
      return false;
    }
  }

  if (!m_file.seek(absoluteOffset)) {
    INX_SERIAL.printf("[ExternalFont] ERR: Seek failed to %u\n", absoluteOffset);
    return false;
  }

  const size_t bytesRead = m_file.read(buffer, length);
  if (bytesRead != length) {
    INX_SERIAL.printf("[ExternalFont] ERR: Read mismatch. Expected %u, got %u\n", length, static_cast<unsigned>(bytesRead));
    return false;
  }

  bitmapCacheStore(absoluteOffset, length, buffer);
  return true;
}
