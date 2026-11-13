#pragma once
#include <cstdint>

struct TestSerial {
  template <typename... Args> void printf(const char*, Args...) {}
  void println(const char*) {}
};
inline TestSerial INX_SERIAL;
