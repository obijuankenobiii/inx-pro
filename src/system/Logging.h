#pragma once

#include <Arduino.h>

#ifndef LOG_INF
#define LOG_INF(tag, format, ...) \
  INX_SERIAL.printf("[%lu] [%s] " format "\n", static_cast<unsigned long>(millis()), tag, ##__VA_ARGS__)
#endif
