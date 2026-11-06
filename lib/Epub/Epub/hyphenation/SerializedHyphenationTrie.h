#pragma once

/**
 * @file SerializedHyphenationTrie.h
 * @brief Public interface and types for SerializedHyphenationTrie.
 */

#include <cstddef>
#include <cstdint>

struct SerializedHyphenationPatterns {
  const std::uint8_t* data;
  size_t size;
  // Newer generated tries store the root offset separately after removing the
  // four-byte prefix from the serialized blob. Zero keeps compatibility with
  // the older embedded format, which stores the root offset in data[0..3].
  std::uint32_t rootOffset = 0;
};
