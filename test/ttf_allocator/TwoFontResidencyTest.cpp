#include "system/ExternalFont.h"
#include "EpdFontFamily.h"
#include <esp_heap_caps.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
std::unordered_map<void*, size_t> allocations;
size_t used = 0;
size_t budget = std::numeric_limits<size_t>::max();
size_t peak = 0;

void renderGlyphs(ExternalFont& font) {
  for (const char cp : std::string("Kiki's Delivery Service")) {
    EpdGlyph glyph{};
    assert(font.getGlyphMetadata(static_cast<uint32_t>(cp), glyph));
    if (glyph.dataLength == 0) continue;
    std::vector<uint8_t> bitmap(glyph.dataLength);
    assert(font.getGlyphBitmap(glyph.dataOffset, glyph.dataLength, bitmap.data()));
    assert(std::any_of(bitmap.begin(), bitmap.end(), [](uint8_t pixel) { return pixel != 0; }));
  }
}

struct LazyFamily {
  std::string path;
  std::array<std::unique_ptr<ExternalFont>, 4> streams;
  std::array<EpdFont, 4> fonts{{EpdFont(nullptr), EpdFont(nullptr), EpdFont(nullptr), EpdFont(nullptr)}};
  EpdFontFamily family{&fonts[0], &fonts[1], &fonts[2], &fonts[3]};
  size_t styleLoads = 0;

  explicit LazyFamily(const char* source) : path(source) {
    streams[0] = std::make_unique<ExternalFont>();
    assert(streams[0]->load(path.c_str(), true, 17));
    fonts[0].data = streams[0]->getData();
    family.setStyleLoader([](void* context, EpdFontFamily::Style style) {
      auto& owner = *static_cast<LazyFamily*>(context);
      ++owner.styleLoads;
      owner.streams[style] = std::make_unique<ExternalFont>();
      const std::string facePath = owner.path + "?face=" + std::to_string(style) + ".ttf";
      assert(owner.streams[style]->load(facePath.c_str(), true, 17));
      owner.family.setData(style, owner.streams[style]->getData());
    }, this);
  }
};
}

void* heap_caps_malloc(size_t bytes, uint32_t caps) {
  assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (bytes > budget - std::min(budget, used)) return nullptr;
  void* memory = std::malloc(bytes);
  if (memory) {
    allocations.emplace(memory, bytes);
    used += bytes;
    peak = std::max(peak, used);
  }
  return memory;
}

void* heap_caps_calloc(size_t count, size_t bytes, uint32_t caps) {
  void* memory = heap_caps_malloc(count * bytes, caps);
  if (memory) std::memset(memory, 0, count * bytes);
  return memory;
}

void heap_caps_free(void* memory) {
  if (!memory) return;
  const auto found = allocations.find(memory);
  assert(found != allocations.end());
  used -= found->second;
  allocations.erase(found);
  std::free(memory);
}
size_t heap_caps_get_free_size(uint32_t) { return budget - used; }
size_t heap_caps_get_largest_free_block(uint32_t) { return budget - used; }

int main(int argc, char** argv) {
  assert(argc == 3);
  budget = 8 * 1024 * 1024;
  {
    LazyFamily system(argv[1]);
    LazyFamily reader(argv[2]);
    assert(system.styleLoads == 0 && reader.styleLoads == 0);
    auto* regular = reader.family.getData();
    assert(reader.family.getData(EpdFontFamily::BOLD) != regular);
    assert(reader.family.getData() == regular);
    assert(system.styleLoads == 0 && reader.styleLoads == 1);
    reader.family.getData(EpdFontFamily::BOLD);
    assert(reader.styleLoads == 1);
    system.family.getData(EpdFontFamily::BOLD);
    assert(system.styleLoads == 1);
    system.family.getLoadedData(EpdFontFamily::ITALIC);
    reader.family.getLoadedData(EpdFontFamily::BOLD_ITALIC);
    assert(system.styleLoads == 1 && reader.styleLoads == 1);
    std::vector<std::unique_ptr<ExternalFont>> sizes;
    for (int size : {8, 10, 12, 14, 16, 21}) {
      auto instance = std::make_unique<ExternalFont>();
      assert(instance->load(size < 16 ? argv[1] : argv[2], false, size));
      sizes.push_back(std::move(instance));
    }
    budget = used + 96 * 1024;
    renderGlyphs(*system.streams[0]);
    renderGlyphs(*system.streams[1]);
    renderGlyphs(*reader.streams[0]);
    renderGlyphs(*reader.streams[1]);
  }
  ExternalFont::clearTtfCache();
  assert(used == 0 && allocations.empty());
  budget = std::numeric_limits<size_t>::max();
  {
    std::vector<std::unique_ptr<ExternalFont>> faces;
    for (int face = 0; face < 9; ++face) {
      auto font = std::make_unique<ExternalFont>();
      const std::string path = std::string(argv[1]) + "?face=" + std::to_string(face) + ".ttf";
      assert(font->load(path.c_str(), false, 16));
      faces.push_back(std::move(font));
    }
    const size_t before = used;
    ExternalFont anotherSize;
    const std::string ninthPath = std::string(argv[1]) + "?face=8.ttf";
    assert(anotherSize.load(ninthPath.c_str(), false, 21));
    assert(used - before < 32 * 1024); // Metadata only, no second full file copy.
    renderGlyphs(anotherSize);
  }
  ExternalFont::clearTtfCache();
  assert(used == 0 && allocations.empty());
  std::printf("Passed: system and reader TTFs render together with regular/bold and six additional sizes; "
              "unused styles stay unloaded; ninth face shares file bytes; no leaks (peak %zu bytes)\n", peak);
}
