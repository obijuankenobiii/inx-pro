// Host regression test of the ESP32 allocation hooks with real stb glyphs.
// clang++ -std=c++17 -DARDUINO_ARCH_ESP32 -Isrc -Itest/ttf_allocator/include \
//   test/ttf_allocator/TtfAllocatorTest.cpp -o /tmp/ttf-allocator-test
// /tmp/ttf-allocator-test /path/to/font.ttf
#include "system/TtfAllocator.h"

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "../../freeink-sdk/libs/book/FreeInkBook/third_party/stb/stb_truetype.h"

#include <cassert>
#include <cstdio>
#include <unordered_map>
#include <vector>

namespace {
std::unordered_map<void*, size_t> allocations;
size_t allocationCount = 0;
size_t expectedEdgePool = 0;
bool allocatedEdgePool = false;
bool psramAvailable = true;
}

void* heap_caps_malloc(const size_t bytes, const uint32_t caps) {
  assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  ++allocationCount;
  if (!psramAvailable) return nullptr;
  void* buffer = std::malloc(bytes);
  assert(buffer);
  allocations.emplace(buffer, bytes);
  if (bytes == expectedEdgePool) allocatedEdgePool = true;
  return buffer;
}

void heap_caps_free(void* buffer) {
  if (!buffer) return;
  assert(allocations.erase(buffer) == 1);
  std::free(buffer);
}

int main(int argc, char** argv) {
  assert(argc == 2);
  FILE* file = std::fopen(argv[1], "rb");
  assert(file);
  assert(std::fseek(file, 0, SEEK_END) == 0);
  const long length = std::ftell(file);
  assert(length > 0 && std::fseek(file, 0, SEEK_SET) == 0);
  std::vector<unsigned char> data(static_cast<size_t>(length));
  assert(std::fread(data.data(), 1, data.size(), file) == data.size());
  std::fclose(file);

  stbtt_fontinfo font{};
  assert(stbtt_InitFont(&font, data.data(), stbtt_GetFontOffsetForIndex(data.data(), 0)));
  const size_t edgeSize = sizeof(stbtt__active_edge);
  expectedEdgePool = sizeof(stbtt__hheap_chunk) + edgeSize * (edgeSize < 32 ? 2000 : edgeSize < 128 ? 800 : 100);
  size_t rendered = 0;
  for (const float points : {12.f, 17.f, 24.f, 36.f}) {
    const float scale = stbtt_ScaleForMappingEmToPixels(&font, points * 150.f / 72.f);
    for (int cp = 33; cp < 127; ++cp) {
      const int glyph = stbtt_FindGlyphIndex(&font, cp);
      if (!glyph) continue;
      int x0, y0, x1, y1;
      stbtt_GetGlyphBitmapBox(&font, glyph, scale, scale, &x0, &y0, &x1, &y1);
      const int width = x1 - x0, height = y1 - y0;
      if (width <= 0 || height <= 0) continue;
      std::vector<unsigned char> bitmap(static_cast<size_t>(width) * height);
      std::vector<unsigned char> repeated(bitmap.size());
      stbtt_MakeGlyphBitmap(&font, bitmap.data(), width, height, width, scale, scale, glyph);
      assert(allocations.empty());
      stbtt_MakeGlyphBitmap(&font, repeated.data(), width, height, width, scale, scale, glyph);
      assert(allocations.empty());
      assert(bitmap == repeated);
      bool hasInk = false;
      for (const auto pixel : bitmap) hasInk |= pixel != 0;
      assert(hasInk);
      ++rendered;
    }
  }
  assert(rendered > 300 && allocationCount > 1000 && allocatedEdgePool);
  // With no PSRAM, do not silently fall back to the internal heap.
  psramAvailable = false;
  assert(inx::ttf::allocate(1024) == nullptr);
  assert(allocations.empty());
  std::printf("Passed: %zu glyphs; %zu scratch allocations routed through PSRAM; active-edge pool verified; no leaks\n",
              rendered, allocationCount);
}
