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
  constexpr uint32_t kMaxStringLen = 65536;
  if (len > kMaxStringLen) {
    s.clear();
    return;
  }
  s.resize(len);
  file.read(&s[0], len);
}
}
