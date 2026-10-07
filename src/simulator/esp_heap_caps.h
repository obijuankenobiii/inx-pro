#pragma once

#ifdef SIMULATOR

#include <cstddef>
#include <cstdlib>

#ifndef MALLOC_CAP_8BIT
#define MALLOC_CAP_8BIT 0
#endif

inline size_t heap_caps_get_total_size(int) { return 1024 * 1024; }
inline size_t heap_caps_get_free_size(int) { return 1024 * 1024; }
inline size_t heap_caps_get_largest_free_block(int) { return 1024 * 1024; }
inline void* heap_caps_malloc(size_t bytes, int) { return std::malloc(bytes); }
inline void* heap_caps_calloc(size_t count, size_t bytes, int) { return std::calloc(count, bytes); }
inline void* heap_caps_realloc(void* pointer, size_t bytes, int) { return std::realloc(pointer, bytes); }
inline void heap_caps_free(void* pointer) { std::free(pointer); }

#else
#error "src/simulator/esp_heap_caps.h is only for simulator builds"
#endif
