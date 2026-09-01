#pragma once

#include <uzlib.h>

#include <cstddef>

enum class InflateStatus {
  Ok,
  Done,
  Error,
};

class InflateReader {
 public:
  InflateReader() = default;
  ~InflateReader();

  InflateReader(const InflateReader&) = delete;
  InflateReader& operator=(const InflateReader&) = delete;

  bool init(bool streaming = false);

  void deinit();

  void setSource(const uint8_t* src, size_t len);

  void setReadCallback(int (*cb)(uzlib_uncomp*));

  void skipZlibHeader();

  bool read(uint8_t* dest, size_t len);

  InflateStatus readAtMost(uint8_t* dest, size_t maxLen, size_t* produced);

  uzlib_uncomp* raw() { return &decomp; }

 private:
  uzlib_uncomp decomp = {};
  uint8_t* ringBuffer = nullptr;
};
