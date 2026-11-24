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
inline bool writePod(FsFile& file, const T& value) {
  return file.write(reinterpret_cast<const uint8_t*>(&value), sizeof(T)) == sizeof(T);
}

template <typename T>
inline void readPod(FsFile& file, T& value) {
  file.read(reinterpret_cast<uint8_t*>(&value), sizeof(T));
}

inline bool writeString(FsFile& file, const std::string& s) {
  if (s.size() > UINT32_MAX) return false;
  const uint32_t len = s.size();
  if (!writePod(file, len)) return false;
  constexpr size_t kWriteChunk = 512;
  size_t offset = 0;
  while (offset < len) {
    const size_t chunk = std::min(kWriteChunk, static_cast<size_t>(len) - offset);
    if (file.write(reinterpret_cast<const uint8_t*>(s.data()) + offset, chunk) != chunk) return false;
    offset += chunk;


    yield();
  }
  return true;
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
