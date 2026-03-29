/**
 * @file ImageDisplayCache.cpp
 * @brief Definitions for ImageDisplayCache.
 */

#include "ImageDisplayCache.h"

#include <Arduino.h>
#include <SDCardManager.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>

#include <esp_heap_caps.h>

#include "GfxRenderer.h"
#include "../../src/util/SdIoMutex.h"
#include "../../src/system/EpubPerf.h"

namespace {
constexpr uint32_t kMagic = 0x43445249;  // IRDC, little-endian on disk
constexpr uint16_t kVersion = 48;        // reject caches written before complete-read and atomic-write validation
constexpr uint32_t kCombinedMagic = 0x32435249;  // IRC2 - distinct from kMagic so a combined file can never be
                                                  // misread as a legacy single-plane one, or vice versa.
constexpr const char* kCacheDir = "/.system/cache";
constexpr size_t kIoBufferSize = 16384;
constexpr size_t kAsyncCacheQueueLength = 6;
// Rendered pixels are expensive to regenerate and the shared SD/display bus is
// the slowest part of a page turn. Keep a small, hard-bounded PSRAM front cache
// in front of the durable SD cache. Entries are individual image planes, so a
// two-bit image consumes two entries and still has correct plane ordering.
constexpr size_t kRamCacheSlots = 64;
constexpr size_t kRamCacheByteBudget = 512 * 1024;
constexpr size_t kSourceSizeCacheSlots = 16;
constexpr uint32_t kSourceSizeReuseMs = 5000;

struct CacheHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t headerSize;
  uint16_t width;
  uint16_t height;
  uint16_t rowBytes;
  uint16_t reserved;
};

// One combined cache file holds both bit-planes back to back: header, then LSB rows, then MSB rows.
// Both planes share width/height/rowBytes since they come from the same source render.
struct CombinedCacheHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t headerSize;
  uint16_t width;
  uint16_t height;
  uint16_t rowBytes;
  uint16_t reserved;
};

struct VisibleRect {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
  int sourceOffsetX = 0;
  int sourceOffsetY = 0;
};

struct AsyncCacheJob {
  std::string cachePath;
  uint16_t width = 0;
  uint16_t height = 0;
  uint16_t rowBytes = 0;
  uint8_t* rows = nullptr;
  bool quality = false;
};

struct RamCacheEntry {
  std::string path;
  uint16_t width = 0;
  uint16_t height = 0;
  uint16_t rowBytes = 0;
  uint8_t* rows = nullptr;
  size_t bytes = 0;
  uint32_t usedAt = 0;
};

struct SourceSizeCacheEntry {
  std::string path;
  uint32_t size = 0;
  uint32_t readAt = 0;
};

// Scratch slot for the one two-bit render that can be in flight at a time (this codebase never renders two
// grayscale images concurrently - one reader page composes at once). Holds each plane's packed rows,
// captured straight from the framebuffer right after that pass renders, so both can be written out
// together as one combined cache entry once the second pass finishes - see captureTwoBitPlane()/
// commitTwoBitCombined().
struct CombinedCaptureState {
  uint8_t* lsbRows = nullptr;
  uint8_t* msbRows = nullptr;
  int width = 0;
  int height = 0;
  int rowBytes = 0;
  bool haveLsb = false;
  bool haveMsb = false;
};
CombinedCaptureState gCombinedCapture;

void resetCombinedCapture() {
  if (gCombinedCapture.lsbRows) heap_caps_free(gCombinedCapture.lsbRows);
  if (gCombinedCapture.msbRows) heap_caps_free(gCombinedCapture.msbRows);
  gCombinedCapture = CombinedCaptureState{};
}

// This is metadata only: image pixels remain in their existing PSRAM front cache. Keeping the
// metadata here avoids permanently reserving internal DRAM for cache entries before any image is
// displayed. All call paths that reach this state already hold SdIoMutex, so first use is serialized.
struct CacheState {
  std::array<RamCacheEntry, kRamCacheSlots> ramCache{};
  std::array<SourceSizeCacheEntry, kSourceSizeCacheSlots> sourceSizeCache{};
  SemaphoreHandle_t ramCacheMutex = nullptr;
  size_t ramCacheBytes = 0;
  uint32_t ramCacheClock = 0;
};

QueueHandle_t gAsyncCacheQueue = nullptr;
SemaphoreHandle_t gAsyncCacheInitMutex = nullptr;
CacheState* gCacheState = nullptr;

CacheState* cacheState() {
  if (gCacheState) {
    return gCacheState;
  }

  void* storage = heap_caps_malloc(sizeof(CacheState), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!storage) {
    storage = heap_caps_malloc(sizeof(CacheState), MALLOC_CAP_8BIT);
  }
  if (!storage) {
    return nullptr;
  }
  gCacheState = new (storage) CacheState();
  return gCacheState;
}

bool lockRamCache() {
  CacheState* state = cacheState();
  if (!state) {
    return false;
  }
  if (!state->ramCacheMutex) {
    state->ramCacheMutex = xSemaphoreCreateMutex();
  }
  return state->ramCacheMutex && xSemaphoreTake(state->ramCacheMutex, pdMS_TO_TICKS(1000)) == pdTRUE;
}

void unlockRamCache() {
  if (gCacheState && gCacheState->ramCacheMutex) {
    xSemaphoreGive(gCacheState->ramCacheMutex);
  }
}

uint32_t fnv1aAdd(uint32_t hash, const uint8_t byte) {
  hash ^= byte;
  return hash * 16777619u;
}

uint32_t fnv1aAddUint32(uint32_t hash, const uint32_t value) {
  hash = fnv1aAdd(hash, static_cast<uint8_t>(value & 0xFF));
  hash = fnv1aAdd(hash, static_cast<uint8_t>((value >> 8) & 0xFF));
  hash = fnv1aAdd(hash, static_cast<uint8_t>((value >> 16) & 0xFF));
  return fnv1aAdd(hash, static_cast<uint8_t>((value >> 24) & 0xFF));
}

uint32_t sourceSize(const std::string& path) {
  // A carousel asks for three cover paths at a time, then asks for the next
  // three immediately after a swipe. Retain a short, bounded source-size LRU
  // so resolving a display-cache key does not reopen every thumbnail on SD.
  const uint32_t now = millis();
  SourceSizeCacheEntry* target = nullptr;
  CacheState* state = cacheState();
  if (state) {
    for (SourceSizeCacheEntry& entry : state->sourceSizeCache) {
      if (entry.path == path) {
        if (now - entry.readAt < kSourceSizeReuseMs) {
          return entry.size;
        }
        target = &entry;
        break;
      }
      if (!target && entry.path.empty()) {
        target = &entry;
      }
    }
    if (!target) {
      target = &*std::min_element(state->sourceSizeCache.begin(), state->sourceSizeCache.end(),
                                  [](const SourceSizeCacheEntry& a, const SourceSizeCacheEntry& b) {
                                    return a.readAt < b.readAt;
                                  });
    }
  }

  FsFile file;
  if (!SdMan.openFileForRead("IDC", path, file)) {
    if (target) {
      target->path = path;
      target->size = 0;
      target->readAt = now;
    }
    return 0;
  }
  const uint32_t size = static_cast<uint32_t>(file.size());
  file.close();
  if (target) {
    target->path = path;
    target->size = size;
    target->readAt = now;
  }
  return size;
}

uint8_t* allocateCacheRows(const size_t bytes) {
  if (auto* psram = static_cast<uint8_t*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT))) {
    return psram;
  }
  return static_cast<uint8_t*>(heap_caps_malloc(bytes, MALLOC_CAP_8BIT));
}

void freeCacheRows(uint8_t* rows) {
  heap_caps_free(rows);
}

// A display cache entry containing only ink is not a useful image cache for an
// EPUB page. This catches the failure mode where a decoder/compositor produced
// a full-black framebuffer and the cache then made that bad result permanent.
// Check logical pixels only so unused bits at the end of a packed row do not
// affect the result.
bool packedRowsAllInk(const uint8_t* rows, const int width, const int height, const int rowBytes) {
  if (!rows || width <= 0 || height <= 0 || rowBytes <= 0) {
    return false;
  }
  for (int row = 0; row < height; ++row) {
    const uint8_t* rowData = rows + static_cast<size_t>(row) * rowBytes;
    for (int col = 0; col < width; ++col) {
      if ((rowData[col / 8] & (0x80 >> (col % 8))) == 0) {
        return false;
      }
    }
  }
  return true;
}

void clearRamEntry(CacheState& state, RamCacheEntry& entry) {
  if (entry.rows) {
    freeCacheRows(entry.rows);
  }
  state.ramCacheBytes -= std::min(state.ramCacheBytes, entry.bytes);
  entry = RamCacheEntry{};
}

bool drawRamCache(GfxRenderer& renderer, const std::string& path, const VisibleRect& visible) {
  CacheState* state = cacheState();
  if (!state || !lockRamCache()) {
    return false;
  }

  RamCacheEntry* hit = nullptr;
  for (RamCacheEntry& entry : state->ramCache) {
    if (entry.rows && entry.path == path && entry.width == visible.width && entry.height == visible.height &&
        entry.rowBytes == static_cast<uint16_t>((visible.width + 7) / 8)) {
      hit = &entry;
      break;
    }
  }
  if (!hit) {
    unlockRamCache();
    return false;
  }

  if (packedRowsAllInk(hit->rows, visible.width, visible.height, hit->rowBytes)) {
    clearRamEntry(*state, *hit);
    unlockRamCache();
    return false;
  }

  hit->usedAt = ++state->ramCacheClock;
  for (int row = 0; row < visible.height; ++row) {
    renderer.drawPackedRow1bpp(visible.x, visible.y + row, visible.width,
                               hit->rows + static_cast<size_t>(row) * hit->rowBytes);
  }
  unlockRamCache();
  return true;
}

bool hasRamCache(const std::string& path) {
  CacheState* state = cacheState();
  if (!state || !lockRamCache()) {
    return false;
  }
  const bool found = std::any_of(state->ramCache.begin(), state->ramCache.end(), [&path](const RamCacheEntry& entry) {
    return entry.rows && entry.path == path;
  });
  unlockRamCache();
  return found;
}

void putRamCache(const std::string& path, const CacheHeader& header, const uint8_t* rows) {
  if (path.empty() || !rows || header.width == 0 || header.height == 0 || header.rowBytes == 0) {
    return;
  }
  const size_t bytes = static_cast<size_t>(header.height) * header.rowBytes;
  CacheState* state = cacheState();
  if (!state || bytes > kRamCacheByteBudget || !lockRamCache()) {
    return;
  }

  RamCacheEntry* target = nullptr;
  for (RamCacheEntry& entry : state->ramCache) {
    if (entry.rows && entry.path == path) {
      clearRamEntry(*state, entry);
      target = &entry;
      break;
    }
    if (!target && !entry.rows) {
      target = &entry;
    }
  }
  while (state->ramCacheBytes + bytes > kRamCacheByteBudget || !target) {
    RamCacheEntry* oldest = nullptr;
    for (RamCacheEntry& entry : state->ramCache) {
      if (entry.rows && (!oldest || entry.usedAt < oldest->usedAt)) {
        oldest = &entry;
      }
    }
    if (!oldest) {
      unlockRamCache();
      return;
    }
    clearRamEntry(*state, *oldest);
    target = oldest;
  }

  uint8_t* copy = allocateCacheRows(bytes);
  if (copy) {
    memcpy(copy, rows, bytes);
    target->path = path;
    target->width = header.width;
    target->height = header.height;
    target->rowBytes = header.rowBytes;
    target->rows = copy;
    target->bytes = bytes;
    target->usedAt = ++state->ramCacheClock;
    state->ramCacheBytes += bytes;
  }
  unlockRamCache();
}

bool cachePathAvailable(const std::string& path) {
  return hasRamCache(path) || SdMan.exists(path.c_str());
}

uint32_t cacheHash(const std::string& sourcePath, const int width, const int height, const VisibleRect& visible,
                   const ImageDisplayCacheOptions& options) {
  uint32_t hash = 2166136261u;
  for (const char c : sourcePath) {
    hash = fnv1aAdd(hash, static_cast<uint8_t>(c));
  }
  hash = fnv1aAddUint32(hash, sourceSize(sourcePath));
  hash = fnv1aAddUint32(hash, static_cast<uint32_t>(width));
  hash = fnv1aAddUint32(hash, static_cast<uint32_t>(height));
  hash = fnv1aAddUint32(hash, static_cast<uint32_t>(visible.sourceOffsetX));
  hash = fnv1aAddUint32(hash, static_cast<uint32_t>(visible.sourceOffsetY));
  hash = fnv1aAddUint32(hash, static_cast<uint32_t>(visible.width));
  hash = fnv1aAddUint32(hash, static_cast<uint32_t>(visible.height));
  hash = fnv1aAdd(hash, options.cropToFill ? 1 : 0);
  // Keep old centered caches valid. Side-carousel caches were previously
  // disabled, so only non-centered anchors need a distinct new key.
  if (options.cropAnchor != 128) {
    hash = fnv1aAdd(hash, options.cropAnchor);
  }
  hash = fnv1aAdd(hash, static_cast<uint8_t>(options.mode));
  hash = fnv1aAdd(hash, options.renderPlane);
  hash = fnv1aAdd(hash, static_cast<uint8_t>(options.roundedOutside));
  hash = fnv1aAdd(hash, options.quality ? 1 : 0);
  return hash;
}

// Same as cacheHash(), minus renderPlane - a combined two-bit entry holds both planes together under one
// key, not one key per plane.
uint32_t cacheHashCombined(const std::string& sourcePath, const int width, const int height,
                           const VisibleRect& visible, const ImageDisplayCacheOptions& options) {
  uint32_t hash = 2166136261u;
  for (const char c : sourcePath) {
    hash = fnv1aAdd(hash, static_cast<uint8_t>(c));
  }
  hash = fnv1aAddUint32(hash, sourceSize(sourcePath));
  hash = fnv1aAddUint32(hash, static_cast<uint32_t>(width));
  hash = fnv1aAddUint32(hash, static_cast<uint32_t>(height));
  hash = fnv1aAddUint32(hash, static_cast<uint32_t>(visible.sourceOffsetX));
  hash = fnv1aAddUint32(hash, static_cast<uint32_t>(visible.sourceOffsetY));
  hash = fnv1aAddUint32(hash, static_cast<uint32_t>(visible.width));
  hash = fnv1aAddUint32(hash, static_cast<uint32_t>(visible.height));
  hash = fnv1aAdd(hash, options.cropToFill ? 1 : 0);
  if (options.cropAnchor != 128) {
    hash = fnv1aAdd(hash, options.cropAnchor);
  }
  hash = fnv1aAdd(hash, static_cast<uint8_t>(options.roundedOutside));
  hash = fnv1aAdd(hash, options.quality ? 1 : 0);
  return hash;
}

bool ensureCacheDir(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  if (slash == std::string::npos) {
    return false;
  }
  const std::string dir = path.substr(0, slash);
  if (dir.empty() || dir == "/") {
    return true;
  }

  auto isDirectory = [](const std::string& p) {
    if (!SdMan.exists(p.c_str())) {
      return false;
    }
    FsFile file = SdMan.open(p.c_str());
    const bool ok = file && file.isDirectory();
    file.close();
    return ok;
  };

  if (isDirectory(dir)) {
    return true;
  }

  size_t pos = 1;
  while (pos < dir.length()) {
    const size_t next = dir.find('/', pos);
    const std::string segment = dir.substr(0, next == std::string::npos ? dir.length() : next);
    if (!segment.empty() && segment != "/" && !isDirectory(segment)) {
      if (!SdMan.mkdir(segment.c_str()) && !isDirectory(segment)) {
        INX_SERIAL.printf("[%lu] [IDC] Failed to create cache dir segment: %s\n", millis(), segment.c_str());
        return false;
      }
      // mkdir can allocate and flush an exFAT directory cluster. Let the idle
      // task run before creating the next segment or writing the cache file.
      vTaskDelay(pdMS_TO_TICKS(1));
    }
    if (next == std::string::npos) {
      break;
    }
    pos = next + 1;
  }

  if (!isDirectory(dir)) {
    INX_SERIAL.printf("[%lu] [IDC] Cache dir missing after mkdir: %s\n", millis(), dir.c_str());
    return false;
  }
  return true;
}

bool writePackedCacheFile(const AsyncCacheJob& job) {
  SdIoMutex::Lock ioLock;
  if (job.cachePath.empty() || !job.rows || job.width == 0 || job.height == 0 || job.rowBytes == 0) {
    return false;
  }
  if (!ensureCacheDir(job.cachePath)) {
    return false;
  }

  const std::string tempPath = job.cachePath + ".tmp";
  FsFile file;
  if (!SdMan.openFileForWrite("IDC", tempPath, file)) {
    if (job.quality) {
      INX_SERIAL.printf("[%lu] [IDC-Q] async store open failed path=%s\n", millis(), tempPath.c_str());
    }
    return false;
  }

  const CacheHeader header = {.magic = kMagic,
                              .version = kVersion,
                              .headerSize = sizeof(CacheHeader),
                              .width = job.width,
                              .height = job.height,
                              .rowBytes = job.rowBytes,
                              .reserved = 0};
  if (file.write(&header, sizeof(header)) != sizeof(header)) {
    file.close();
    SdMan.remove(tempPath.c_str());
    return false;
  }
  vTaskDelay(pdMS_TO_TICKS(1));

  const size_t totalBytes = static_cast<size_t>(job.height) * job.rowBytes;
  size_t offset = 0;
  while (offset < totalBytes) {
    const size_t chunk = std::min(kIoBufferSize, totalBytes - offset);
    if (file.write(job.rows + offset, chunk) != chunk) {
      file.close();
      SdMan.remove(tempPath.c_str());
      return false;
    }
    offset += chunk;
    yield();
  }
  file.close();
  SdMan.remove(job.cachePath.c_str());
  if (!SdMan.rename(tempPath.c_str(), job.cachePath.c_str())) {
    SdMan.remove(tempPath.c_str());
    return false;
  }
  return true;
}

bool ensureDeferredCacheQueue() {
  if (gAsyncCacheQueue) {
    return true;
  }
  if (!gAsyncCacheInitMutex) {
    gAsyncCacheInitMutex = xSemaphoreCreateMutex();
  }
  if (!gAsyncCacheInitMutex || xSemaphoreTake(gAsyncCacheInitMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    return false;
  }

  bool ready = gAsyncCacheQueue != nullptr;
  if (!ready) {
    if (!gAsyncCacheQueue) {
      gAsyncCacheQueue = xQueueCreate(kAsyncCacheQueueLength, sizeof(AsyncCacheJob*));
    }
    ready = gAsyncCacheQueue != nullptr;
  }
  xSemaphoreGive(gAsyncCacheInitMutex);
  return ready;
}

bool visibleBounds(GfxRenderer& renderer, const int x, const int y, const int width, const int height,
                   VisibleRect& out) {
  if (width <= 0 || height <= 0) {
    return false;
  }
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const int x1 = std::max(0, x);
  const int y1 = std::max(0, y);
  const int x2 = std::min(screenW, x + width);
  const int y2 = std::min(screenH, y + height);
  if (x2 <= x1 || y2 <= y1) {
    return false;
  }
  out.x = x1;
  out.y = y1;
  out.width = x2 - x1;
  out.height = y2 - y1;
  out.sourceOffsetX = x1 - x;
  out.sourceOffsetY = y1 - y;
  return true;
}

const char* planeName(const ImageDisplayCacheOptions& options) {
  switch (static_cast<GfxRenderer::RenderMode>(options.renderPlane)) {
    case GfxRenderer::GRAYSCALE_LSB:
      return "GRAYSCALE_LSB";
    case GfxRenderer::GRAYSCALE_MSB:
      return "GRAYSCALE_MSB";
    case GfxRenderer::GRAY2_LSB:
      return "GRAY2_LSB";
    case GfxRenderer::GRAY2_MSB:
      return "GRAY2_MSB";
    case GfxRenderer::BW:
    default:
      return "BW";
  }
}

// Caller owns SdIoMutex. Loading a durable cache into PSRAM is intentionally
// separate from drawing it so Home can prepare neighbouring carousel covers
// while the current screen is already visible.
bool loadSdCacheIntoRam(const std::string& cachePath, const VisibleRect& visible,
                        const ImageDisplayCacheOptions& options) {
  if (!SdMan.exists(cachePath.c_str())) {
    return false;
  }

  FsFile file;
  if (!SdMan.openFileForRead("IDC", cachePath, file)) {
    if (options.quality) {
      INX_SERIAL.printf("[%lu] [IDC-Q] cache open failed plane=%s path=%s\n", millis(), planeName(options),
                        cachePath.c_str());
    }
    return false;
  }

  CacheHeader header;
  const bool headerOk = file.read(&header, sizeof(header)) == sizeof(header) && header.magic == kMagic &&
                        header.version == kVersion && header.headerSize == sizeof(CacheHeader) &&
                        header.width == visible.width && header.height == visible.height &&
                        header.rowBytes == static_cast<uint16_t>((visible.width + 7) / 8);
  if (!headerOk) {
    if (options.quality) {
      INX_SERIAL.printf(
          "[%lu] [IDC-Q] cache header invalid plane=%s path=%s magic=%08lx ver=%u header=%u wh=%ux%u row=%u "
          "expected=%dx%d/%d\n",
          millis(), planeName(options), cachePath.c_str(), static_cast<unsigned long>(header.magic), header.version,
          header.headerSize, header.width, header.height, header.rowBytes, visible.width, visible.height,
          (visible.width + 7) / 8);
    }
    file.close();
    return false;
  }

  const size_t dataBytes = static_cast<size_t>(visible.height) * header.rowBytes;
  const uint64_t expectedFileSize = sizeof(CacheHeader) + static_cast<uint64_t>(dataBytes);
  if (static_cast<uint64_t>(file.size()) != expectedFileSize) {
    if (options.quality) {
      INX_SERIAL.printf("[%lu] [IDC-Q] cache size invalid plane=%s path=%s got=%lu expected=%lu\n", millis(),
                        planeName(options), cachePath.c_str(), static_cast<unsigned long>(file.size()),
                        static_cast<unsigned long>(expectedFileSize));
    }
    file.close();
    SdMan.remove(cachePath.c_str());
    return false;
  }

  std::unique_ptr<uint8_t, decltype(&freeCacheRows)> rows(allocateCacheRows(dataBytes), freeCacheRows);
  if (!rows) {
    file.close();
    return false;
  }

  size_t offset = 0;
  while (offset < dataBytes) {
    const size_t chunk = std::min(kIoBufferSize, dataBytes - offset);
    if (file.read(rows.get() + offset, chunk) != chunk) {
      if (options.quality) {
        INX_SERIAL.printf("[%lu] [IDC-Q] cache read failed plane=%s path=%s offset=%lu/%lu\n", millis(),
                          planeName(options), cachePath.c_str(), static_cast<unsigned long>(offset),
                          static_cast<unsigned long>(dataBytes));
      }
      file.close();
      SdMan.remove(cachePath.c_str());
      return false;
    }
    offset += chunk;
  }
  file.close();
  if (packedRowsAllInk(rows.get(), visible.width, visible.height, header.rowBytes)) {
    SdMan.remove(cachePath.c_str());
    return false;
  }
  putRamCache(cachePath, header, rows.get());
  return hasRamCache(cachePath);
}

}  // namespace

std::string ImageDisplayCache::pathFor(GfxRenderer& renderer, const std::string& sourcePath, const int x, const int y,
                                       const int width, const int height, const ImageDisplayCacheOptions& options) {
  VisibleRect visible;
  if (!visibleBounds(renderer, x, y, width, height, visible)) {
    return "";
  }
  const uint32_t hash = cacheHash(sourcePath, width, height, visible, options);
  char name[48];
  snprintf(name, sizeof(name), "/%02lx/%08lx.irdc", static_cast<unsigned long>((hash >> 24) & 0xFF),
           static_cast<unsigned long>(hash));
  return std::string(kCacheDir) + name;
}

std::string ImageDisplayCache::pathForCombined(GfxRenderer& renderer, const std::string& sourcePath, const int x,
                                               const int y, const int width, const int height,
                                               const ImageDisplayCacheOptions& options) {
  VisibleRect visible;
  if (!visibleBounds(renderer, x, y, width, height, visible)) {
    return "";
  }
  const uint32_t hash = cacheHashCombined(sourcePath, width, height, visible, options);
  char name[48];
  // .irdc2 extension keeps combined entries out of the legacy single-plane sweep/lookup paths.
  snprintf(name, sizeof(name), "/%02lx/%08lx.irdc2", static_cast<unsigned long>((hash >> 24) & 0xFF),
           static_cast<unsigned long>(hash));
  return std::string(kCacheDir) + name;
}

bool ImageDisplayCache::renderIfAvailable(GfxRenderer& renderer, const std::string& sourcePath, const int x,
                                          const int y, const int width, const int height,
                                          const ImageDisplayCacheOptions& options) {
  SdIoMutex::Lock ioLock;
  VisibleRect visible;
  if (!visibleBounds(renderer, x, y, width, height, visible)) {
    return false;
  }

  const std::string requestedPath = pathFor(renderer, sourcePath, x, y, width, height, options);
  if (requestedPath.empty()) {
    return false;
  }

  // Cache files are written from the active renderer plane. Replay the same
  // plane into the same framebuffer/controller plane; swapping LSB and MSB
  // here changes every two-bit gray value on cache hits.
  if (!drawRamCache(renderer, requestedPath, visible) && !loadSdCacheIntoRam(requestedPath, visible, options)) {
    return false;
  }
  return drawRamCache(renderer, requestedPath, visible);
}

bool ImageDisplayCache::preloadIfAvailable(GfxRenderer& renderer, const std::string& sourcePath, const int x,
                                           const int y, const int width, const int height,
                                           const ImageDisplayCacheOptions& options) {
  SdIoMutex::Lock ioLock;
  VisibleRect visible;
  if (!visibleBounds(renderer, x, y, width, height, visible)) {
    return false;
  }
  const std::string cachePath = pathFor(renderer, sourcePath, x, y, width, height, options);
  if (cachePath.empty() || hasRamCache(cachePath)) {
    return !cachePath.empty();
  }
  return loadSdCacheIntoRam(cachePath, visible, options);
}

bool ImageDisplayCache::displayTwoBitIfAvailable(GfxRenderer& renderer, const std::string& sourcePath, const int x,
                                                 const int y, const int width, const int height,
                                                 const ImageDisplayCacheOptions& options, const bool quality,
                                                 const bool fastQuality) {
  SdIoMutex::Lock ioLock;
  ImageDisplayCacheOptions lsbOptions = options;
  lsbOptions.mode = ImageRenderMode::TwoBit;
  lsbOptions.renderPlane = static_cast<uint8_t>(quality ? GfxRenderer::GRAY2_LSB : GfxRenderer::GRAYSCALE_LSB);
  lsbOptions.quality = quality;

  ImageDisplayCacheOptions msbOptions = options;
  msbOptions.mode = ImageRenderMode::TwoBit;
  msbOptions.renderPlane = static_cast<uint8_t>(quality ? GfxRenderer::GRAY2_MSB : GfxRenderer::GRAYSCALE_MSB);
  msbOptions.quality = quality;

  const std::string lsbPath = pathFor(renderer, sourcePath, x, y, width, height, lsbOptions);
  const std::string msbPath = pathFor(renderer, sourcePath, x, y, width, height, msbOptions);
  if (lsbPath.empty() || msbPath.empty() || !cachePathAvailable(lsbPath) || !cachePathAvailable(msbPath)) {
    return false;
  }

  const bool useFastQuality = quality && fastQuality;

  if (quality) {
    renderer.prepareQualityGrayscale();
  }

  renderer.clearScreen(quality ? 0xFF : 0x00);
  renderer.setRenderMode(quality ? GfxRenderer::GRAY2_LSB : GfxRenderer::GRAYSCALE_LSB);
  if (!renderIfAvailable(renderer, sourcePath, x, y, width, height, lsbOptions)) {
    renderer.setRenderMode(GfxRenderer::BW);
    return false;
  }
  renderer.copyGrayscaleLsbBuffers();

  renderer.clearScreen(quality ? 0xFF : 0x00);
  renderer.setRenderMode(quality ? GfxRenderer::GRAY2_MSB : GfxRenderer::GRAYSCALE_MSB);
  if (!renderIfAvailable(renderer, sourcePath, x, y, width, height, msbOptions)) {
    renderer.setRenderMode(GfxRenderer::BW);
    return false;
  }
  renderer.copyGrayscaleMsbBuffers();

  if (useFastQuality) {
    renderer.displayGrayBufferFastQuality();
  } else {
    renderer.displayGrayBuffer(quality);
  }
  renderer.setRenderMode(GfxRenderer::BW);
  renderer.clearScreen(0xFF);
  renderer.cleanupGrayscaleWithFrameBuffer();

  return true;
}

bool ImageDisplayCache::hasCachedTwoBit(GfxRenderer& renderer, const std::string& sourcePath, const int x, const int y,
                                        const int width, const int height, const ImageDisplayCacheOptions& options,
                                        const bool quality) {
  SdIoMutex::Lock ioLock;
  ImageDisplayCacheOptions lsbOptions = options;
  lsbOptions.mode = ImageRenderMode::TwoBit;
  lsbOptions.renderPlane = static_cast<uint8_t>(quality ? GfxRenderer::GRAY2_LSB : GfxRenderer::GRAYSCALE_LSB);
  lsbOptions.quality = quality;

  ImageDisplayCacheOptions msbOptions = options;
  msbOptions.mode = ImageRenderMode::TwoBit;
  msbOptions.renderPlane = static_cast<uint8_t>(quality ? GfxRenderer::GRAY2_MSB : GfxRenderer::GRAYSCALE_MSB);
  msbOptions.quality = quality;

  const std::string lsbPath = pathFor(renderer, sourcePath, x, y, width, height, lsbOptions);
  const std::string msbPath = pathFor(renderer, sourcePath, x, y, width, height, msbOptions);
  return !lsbPath.empty() && !msbPath.empty() && cachePathAvailable(lsbPath) && cachePathAvailable(msbPath);
}

bool ImageDisplayCache::hasCombinedTwoBit(GfxRenderer& renderer, const std::string& sourcePath, const int x,
                                          const int y, const int width, const int height,
                                          const ImageDisplayCacheOptions& options) {
  SdIoMutex::Lock ioLock;
  const std::string path = pathForCombined(renderer, sourcePath, x, y, width, height, options);
  return !path.empty() && SdMan.exists(path.c_str());
}

bool ImageDisplayCache::renderCombinedTwoBit(GfxRenderer& renderer, const std::string& sourcePath, const int x,
                                             const int y, const int width, const int height,
                                             const ImageDisplayCacheOptions& options, const bool quality,
                                             const bool fastQuality) {
  SdIoMutex::Lock ioLock;
  VisibleRect visible;
  if (!visibleBounds(renderer, x, y, width, height, visible)) {
    return false;
  }

  ImageDisplayCacheOptions combinedOptions = options;
  combinedOptions.quality = quality;
  const std::string path = pathForCombined(renderer, sourcePath, x, y, width, height, combinedOptions);
  if (path.empty()) {
    return false;
  }

  FsFile file;
  if (!SdMan.openFileForRead("IDC", path, file)) {
    return false;
  }

  const int expectedRowBytes = (visible.width + 7) / 8;
  CombinedCacheHeader header;
  const bool headerOk = file.read(&header, sizeof(header)) == sizeof(header) && header.magic == kCombinedMagic &&
                        header.version == kVersion && header.headerSize == sizeof(CombinedCacheHeader) &&
                        header.width == visible.width && header.height == visible.height &&
                        header.rowBytes == expectedRowBytes;
  if (!headerOk) {
    file.close();
    return false;
  }

  const size_t planeBytes = static_cast<size_t>(header.height) * header.rowBytes;
  const uint64_t expectedFileSize = sizeof(CombinedCacheHeader) + static_cast<uint64_t>(planeBytes) * 2;
  if (static_cast<uint64_t>(file.size()) != expectedFileSize) {
    file.close();
    SdMan.remove(path.c_str());
    return false;
  }

  std::unique_ptr<uint8_t, decltype(&freeCacheRows)> lsbRows(allocateCacheRows(planeBytes), freeCacheRows);
  std::unique_ptr<uint8_t, decltype(&freeCacheRows)> msbRows(allocateCacheRows(planeBytes), freeCacheRows);
  if (!lsbRows || !msbRows) {
    file.close();
    return false;
  }

  auto readPlane = [&](uint8_t* rows) {
    size_t offset = 0;
    while (offset < planeBytes) {
      const size_t chunk = std::min(kIoBufferSize, planeBytes - offset);
      if (file.read(rows + offset, chunk) != chunk) {
        return false;
      }
      offset += chunk;
    }
    return true;
  };
  if (!readPlane(lsbRows.get()) || !readPlane(msbRows.get())) {
    file.close();
    SdMan.remove(path.c_str());
    return false;
  }
  if (packedRowsAllInk(lsbRows.get(), visible.width, visible.height, header.rowBytes) &&
      packedRowsAllInk(msbRows.get(), visible.width, visible.height, header.rowBytes)) {
    file.close();
    SdMan.remove(path.c_str());
    return false;
  }
  file.close();

  if (quality) {
    renderer.prepareQualityGrayscale();
  }

  renderer.clearScreen(quality ? 0xFF : 0x00);
  renderer.setRenderMode(quality ? GfxRenderer::GRAY2_LSB : GfxRenderer::GRAYSCALE_LSB);
  for (int row = 0; row < visible.height; ++row) {
    renderer.drawPackedRow1bpp(visible.x, visible.y + row, visible.width,
                               lsbRows.get() + static_cast<size_t>(row) * header.rowBytes);
  }
  renderer.copyGrayscaleLsbBuffers();

  renderer.clearScreen(quality ? 0xFF : 0x00);
  renderer.setRenderMode(quality ? GfxRenderer::GRAY2_MSB : GfxRenderer::GRAYSCALE_MSB);
  for (int row = 0; row < visible.height; ++row) {
    renderer.drawPackedRow1bpp(visible.x, visible.y + row, visible.width,
                               msbRows.get() + static_cast<size_t>(row) * header.rowBytes);
  }
  renderer.copyGrayscaleMsbBuffers();

  if (quality && fastQuality) {
    renderer.displayGrayBufferFastQuality();
  } else {
    renderer.displayGrayBuffer(quality);
  }
  renderer.setRenderMode(GfxRenderer::BW);
  renderer.clearScreen(0xFF);
  renderer.cleanupGrayscaleWithFrameBuffer();

  return true;
}

bool ImageDisplayCache::captureTwoBitPlane(GfxRenderer& renderer, const int x, const int y, const int width,
                                           const int height, const bool isMsbPlane) {
  SdIoMutex::Lock ioLock;
  VisibleRect visible;
  if (!visibleBounds(renderer, x, y, width, height, visible)) {
    return false;
  }

  if (gCombinedCapture.width != visible.width || gCombinedCapture.height != visible.height) {
    resetCombinedCapture();
  }

  const int rowBytes = (visible.width + 7) / 8;
  const size_t planeBytes = static_cast<size_t>(rowBytes) * visible.height;
  uint8_t*& target = isMsbPlane ? gCombinedCapture.msbRows : gCombinedCapture.lsbRows;
  if (!target) {
    target = allocateCacheRows(planeBytes);
    if (!target) {
      return false;
    }
  }

  for (int row = 0; row < visible.height; ++row) {
    renderer.readPackedRow1bpp(visible.x, visible.y + row, visible.width, target + static_cast<size_t>(row) * rowBytes);
  }

  gCombinedCapture.width = visible.width;
  gCombinedCapture.height = visible.height;
  gCombinedCapture.rowBytes = rowBytes;
  if (isMsbPlane) {
    gCombinedCapture.haveMsb = true;
  } else {
    gCombinedCapture.haveLsb = true;
  }
  return true;
}

bool ImageDisplayCache::commitTwoBitCombined(GfxRenderer& renderer, const std::string& sourcePath, const int x,
                                             const int y, const int width, const int height,
                                             const ImageDisplayCacheOptions& options) {
  SdIoMutex::Lock ioLock;
  if (!gCombinedCapture.haveLsb || !gCombinedCapture.haveMsb) {
    return false;
  }

  VisibleRect visible;
  if (!visibleBounds(renderer, x, y, width, height, visible) || visible.width != gCombinedCapture.width ||
      visible.height != gCombinedCapture.height) {
    resetCombinedCapture();
    return false;
  }

  const std::string path = pathForCombined(renderer, sourcePath, x, y, width, height, options);
  if (path.empty() || !ensureCacheDir(path)) {
    resetCombinedCapture();
    return false;
  }

  const std::string tempPath = path + ".tmp";
  FsFile file;
  if (!SdMan.openFileForWrite("IDC", tempPath, file)) {
    resetCombinedCapture();
    return false;
  }

  const CombinedCacheHeader header = {.magic = kCombinedMagic,
                                      .version = kVersion,
                                      .headerSize = sizeof(CombinedCacheHeader),
                                      .width = static_cast<uint16_t>(gCombinedCapture.width),
                                      .height = static_cast<uint16_t>(gCombinedCapture.height),
                                      .rowBytes = static_cast<uint16_t>(gCombinedCapture.rowBytes),
                                      .reserved = 0};
  if (file.write(&header, sizeof(header)) != sizeof(header)) {
    file.close();
    SdMan.remove(tempPath.c_str());
    resetCombinedCapture();
    return false;
  }

  const size_t planeBytes = static_cast<size_t>(gCombinedCapture.rowBytes) * gCombinedCapture.height;
  auto writePlane = [&](const uint8_t* rows) {
    size_t offset = 0;
    while (offset < planeBytes) {
      const size_t chunk = std::min(kIoBufferSize, planeBytes - offset);
      if (file.write(rows + offset, chunk) != chunk) {
        return false;
      }
      offset += chunk;
      vTaskDelay(pdMS_TO_TICKS(1));
    }
    return true;
  };
  if (!writePlane(gCombinedCapture.lsbRows) || !writePlane(gCombinedCapture.msbRows)) {
    file.close();
    SdMan.remove(tempPath.c_str());
    resetCombinedCapture();
    return false;
  }

  file.close();
  SdMan.remove(path.c_str());
  const bool renamed = SdMan.rename(tempPath.c_str(), path.c_str());
  if (!renamed) {
    SdMan.remove(tempPath.c_str());
  }
  resetCombinedCapture();
  return renamed;
}

void ImageDisplayCache::cancelTwoBitCapture() {
  resetCombinedCapture();
}

bool ImageDisplayCache::store(GfxRenderer& renderer, const std::string& sourcePath, const int x, const int y,
                              const int width, const int height, const ImageDisplayCacheOptions& options) {
  SdIoMutex::Lock ioLock;
  VisibleRect visible;
  if (!visibleBounds(renderer, x, y, width, height, visible)) {
    return false;
  }

  const std::string cachePath = pathFor(renderer, sourcePath, x, y, width, height, options);
  if (cachePath.empty()) {
    if (options.quality) {
      INX_SERIAL.printf("[%lu] [IDC-Q] store path empty plane=%s src=%s rect=%d,%d %dx%d\n", millis(), planeName(options),
                    sourcePath.c_str(), x, y, width, height);
    }
    return false;
  }
  if (!ensureCacheDir(cachePath)) {
    return false;
  }

  const std::string tempPath = cachePath + ".tmp";
  FsFile file;
  if (!SdMan.openFileForWrite("IDC", tempPath, file)) {
    if (options.quality) {
      INX_SERIAL.printf("[%lu] [IDC-Q] store open failed plane=%s path=%s\n", millis(), planeName(options),
                    cachePath.c_str());
    }
    return false;
  }

  const int rowBytes = (visible.width + 7) / 8;
  if (rowBytes > static_cast<int>(kIoBufferSize)) {
    file.close();
    SdMan.remove(tempPath.c_str());
    return false;
  }
  const size_t dataBytes = static_cast<size_t>(rowBytes) * static_cast<size_t>(visible.height);
  std::unique_ptr<uint8_t, decltype(&freeCacheRows)> rows(allocateCacheRows(dataBytes), freeCacheRows);
  if (!rows) {
    file.close();
    SdMan.remove(tempPath.c_str());
    return false;
  }

  const CacheHeader header = {.magic = kMagic,
                              .version = kVersion,
                              .headerSize = sizeof(CacheHeader),
                              .width = static_cast<uint16_t>(visible.width),
                              .height = static_cast<uint16_t>(visible.height),
                              .rowBytes = static_cast<uint16_t>(rowBytes),
                              .reserved = 0};
  if (file.write(&header, sizeof(header)) != sizeof(header)) {
    file.close();
    SdMan.remove(tempPath.c_str());
    return false;
  }

  for (int row = 0; row < visible.height; ++row) {
    renderer.readPackedRow1bpp(visible.x, visible.y + row, visible.width,
                               rows.get() + static_cast<size_t>(row) * rowBytes);
  }

  if (packedRowsAllInk(rows.get(), visible.width, visible.height, rowBytes)) {
    file.close();
    SdMan.remove(tempPath.c_str());
    return false;
  }

  const int rowsPerWrite = std::max(1, static_cast<int>(kIoBufferSize) / rowBytes);
  for (int rowBase = 0; rowBase < visible.height; rowBase += rowsPerWrite) {
    const int rowsThisWrite = std::min(rowsPerWrite, visible.height - rowBase);
    const int bytesThisWrite = rowsThisWrite * rowBytes;
    const uint8_t* writeAt = rows.get() + static_cast<size_t>(rowBase) * rowBytes;
    if (file.write(writeAt, bytesThisWrite) != static_cast<size_t>(bytesThisWrite)) {
      if (options.quality) {
        INX_SERIAL.printf("[%lu] [IDC-Q] store row write failed plane=%s path=%s row=%d/%d\n", millis(), planeName(options),
                      cachePath.c_str(), rowBase, visible.height);
      }
      file.close();
      SdMan.remove(tempPath.c_str());
      return false;
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }

  file.close();
  vTaskDelay(pdMS_TO_TICKS(1));
  SdMan.remove(cachePath.c_str());
  vTaskDelay(pdMS_TO_TICKS(1));
  if (!SdMan.rename(tempPath.c_str(), cachePath.c_str())) {
    SdMan.remove(tempPath.c_str());
    return false;
  }

  putRamCache(cachePath, header, rows.get());
  return true;
}

bool ImageDisplayCache::storeAsync(GfxRenderer& renderer, const std::string& sourcePath, const int x, const int y,
                                   const int width, const int height, const ImageDisplayCacheOptions& options) {
  SdIoMutex::Lock ioLock;

  VisibleRect visible;
  if (!visibleBounds(renderer, x, y, width, height, visible)) {
    return false;
  }
  const std::string cachePath = pathFor(renderer, sourcePath, x, y, width, height, options);
  if (cachePath.empty()) {
    return false;
  }

  const size_t rowBytes = (static_cast<size_t>(visible.width) + 7u) / 8u;
  const size_t totalBytes = rowBytes * static_cast<size_t>(visible.height);
  uint8_t* packedRows = allocateCacheRows(totalBytes);
  if (!packedRows) {
    return false;
  }

  for (int row = 0; row < visible.height; ++row) {
    renderer.readPackedRow1bpp(visible.x, visible.y + row, visible.width, packedRows + row * rowBytes);
    if ((row & 31) == 31) {
      yield();
    }
  }

  if (packedRowsAllInk(packedRows, visible.width, visible.height, static_cast<int>(rowBytes))) {
    freeCacheRows(packedRows);
    return false;
  }

  if (!ensureDeferredCacheQueue()) {
    freeCacheRows(packedRows);
    return false;
  }

  auto* job = new (std::nothrow) AsyncCacheJob();
  if (!job) {
    freeCacheRows(packedRows);
    return false;
  }
  job->cachePath = cachePath;
  job->width = static_cast<uint16_t>(visible.width);
  job->height = static_cast<uint16_t>(visible.height);
  job->rowBytes = static_cast<uint16_t>(rowBytes);
  job->rows = packedRows;
  job->quality = options.quality;

  // The page can use this plane immediately, even before the low-priority SD
  // writer has persisted it. This is especially important for the two planes
  // of a grayscale image: the second pass must not wait for the first cache
  // file to reach the card.
  const CacheHeader header = {.magic = kMagic,
                              .version = kVersion,
                              .headerSize = sizeof(CacheHeader),
                              .width = job->width,
                              .height = job->height,
                              .rowBytes = job->rowBytes,
                              .reserved = 0};
  putRamCache(cachePath, header, packedRows);

  if (xQueueSend(gAsyncCacheQueue, &job, 0) != pdTRUE) {
    freeCacheRows(packedRows);
    delete job;
    return false;
  }
  return true;
}

void ImageDisplayCache::flushDeferredWrites(const uint8_t maximumJobs) {
  if (!gAsyncCacheQueue || maximumJobs == 0) {
    return;
  }

  // Do not use a worker task here. SdFat/Arduino SPI keeps transaction state
  // per task, while several metadata reads occur on the loop task without an
  // external mutex. Persisting in this same task keeps the SPI owner stable.
  for (uint8_t i = 0; i < maximumJobs; ++i) {
    AsyncCacheJob* job = nullptr;
    if (xQueueReceive(gAsyncCacheQueue, &job, 0) != pdTRUE || !job) {
      return;
    }
    writePackedCacheFile(*job);
    freeCacheRows(job->rows);
    delete job;
    yield();
  }
}

bool ImageDisplayCache::hasCached(GfxRenderer& renderer, const std::string& sourcePath, const int x, const int y,
                                  const int width, const int height, const ImageDisplayCacheOptions& options) {
  SdIoMutex::Lock ioLock;
  const std::string cachePath = pathFor(renderer, sourcePath, x, y, width, height, options);
  return !cachePath.empty() && cachePathAvailable(cachePath);
}
