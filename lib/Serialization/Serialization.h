#pragma once

/**
 * @file Serialization.h
 * @brief Public interface and types for Serialization.
 */

#include <SdFat.h>

#include <Arduino.h>

#include <algorithm>
#include <cstdint>
#include <string>

namespace serialization {
template <typename T>
inline void writePod(FsFile& file, const T& value) {
  file.write(reinterpret_cast<const uint8_t*>(&value), sizeof(T));
}

template <typename T>
inline void readPod(FsFile& file, T& value) {
  file.read(reinterpret_cast<uint8_t*>(&value), sizeof(T));
}

inline void writeString(FsFile& file, const std::string& s) {
  const uint32_t len = s.size();
  writePod(file, len);
  // A long CSS entry or metadata string can otherwise keep SdFat inside one
  // synchronous write long enough to starve the ESP32 idle task. Chunking also
  // keeps the operation responsive on the slower SD SPI path.
  constexpr size_t kWriteChunk = 512;
  size_t offset = 0;
  while (offset < len) {
    const size_t chunk = std::min(kWriteChunk, static_cast<size_t>(len) - offset);
    file.write(reinterpret_cast<const uint8_t*>(s.data()) + offset, chunk);
    offset += chunk;
    delay(1);
  }
}

inline void readString(FsFile& file, std::string& s) {
  uint32_t len;
  readPod(file, len);
  // Guards against a corrupted/misaligned cache file (e.g. a parallel per-word list that drifted out of
  // sync with word count before being written - see TextBlock::serialize()'s size checks) turning a
  // garbage length prefix into an unbounded allocation; std::string::resize() throws std::bad_alloc on
  // failure, which is uncaught here and takes the whole device down. No legitimate string written by this
  // codebase (a word, a file path, a footnote target) comes anywhere close to this size.
  constexpr uint32_t kMaxStringLen = 65536;
  if (len > kMaxStringLen) {
    s.clear();
    return;
  }
  s.resize(len);
  file.read(&s[0], len);
}
}  // namespace serialization
