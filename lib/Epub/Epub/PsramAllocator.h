#pragma once

#include <cstddef>
#include <cstdlib>
#include <new>
#include <string>

#if defined(ARDUINO_ARCH_ESP32)
#include <esp_heap_caps.h>
#endif

/** Small STL allocator that keeps large, bounded EPUB indexes in PSRAM. */
template <typename T>
class EpubPsramAllocator {
 public:
  using value_type = T;

  EpubPsramAllocator() noexcept = default;

  template <typename U>
  EpubPsramAllocator(const EpubPsramAllocator<U>&) noexcept {}

  T* allocate(const std::size_t count) {
    if (count > static_cast<std::size_t>(-1) / sizeof(T)) throw std::bad_array_new_length();
    const std::size_t bytes = count * sizeof(T);
#if defined(ARDUINO_ARCH_ESP32)
    if (void* memory = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)) {
      return static_cast<T*>(memory);
    }
#endif
    if (void* memory = std::malloc(bytes)) return static_cast<T*>(memory);
    throw std::bad_alloc();
  }

  void deallocate(T* memory, std::size_t) noexcept { std::free(memory); }

  template <typename U>
  bool operator==(const EpubPsramAllocator<U>&) const noexcept {
    return true;
  }

  template <typename U>
  bool operator!=(const EpubPsramAllocator<U>&) const noexcept {
    return false;
  }
};

using EpubPsramString = std::basic_string<char, std::char_traits<char>, EpubPsramAllocator<char>>;
