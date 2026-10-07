#pragma once

#include <cstddef>
#include <cstdint>

namespace freeink {

class Microphone {
 public:
  bool begin(uint32_t) { return false; }
  void end() {}
  int read(int16_t *, size_t, uint32_t) { return 0; }
};

}  // namespace freeink
