#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>

#if defined(ARDUINO_ARCH_ESP32)
#include <esp_heap_caps.h>
#endif

namespace inx::ttf {
inline uint8_t* allocate(const size_t bytes) {
#if defined(ARDUINO_ARCH_ESP32)
  return static_cast<uint8_t*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#else
  return static_cast<uint8_t*>(std::malloc(bytes));
#endif
}

inline void release(void* buffer) {
#if defined(ARDUINO_ARCH_ESP32)
  heap_caps_free(buffer);
#else
  std::free(buffer);
#endif
}
}  // namespace inx::ttf

// stb's outline, scanline and active-edge storage must follow the same PSRAM
// policy as font bytes and glyph bitmaps. Its default allocator is plain malloc.
#define STBTT_malloc(bytes, userdata) ((void)(userdata), inx::ttf::allocate(bytes))
#define STBTT_free(buffer, userdata) ((void)(userdata), inx::ttf::release(buffer))
