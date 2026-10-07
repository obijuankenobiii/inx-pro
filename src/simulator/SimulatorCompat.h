#pragma once

#ifdef SIMULATOR

#ifdef __cplusplus
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstdint>
#else
#include <stdint.h>
#endif

#ifdef __cplusplus
using TickType_t = uint32_t;
using BaseType_t = int;
using UBaseType_t = unsigned int;
#else
typedef uint32_t TickType_t;
typedef int BaseType_t;
typedef unsigned int UBaseType_t;
#endif

#ifndef pdPASS
#define pdPASS 1
#endif

#ifndef pdMS_TO_TICKS
#define pdMS_TO_TICKS(ms) (ms)
#endif

#ifndef ARDUINO_RUNNING_CORE
#define ARDUINO_RUNNING_CORE 0
#endif

#ifndef FREEINK_DEVICE_X4PRO
#if defined(SIMULATOR_DEVICE_X4_PRO)
#define FREEINK_DEVICE_X4PRO 1
#else
#define FREEINK_DEVICE_X4PRO 0
#endif
#endif
#ifndef FREEINK_DEVICE_MURPHY_M4
#define FREEINK_DEVICE_MURPHY_M4 0
#endif
#ifndef FREEINK_DEVICE_STICKY
#define FREEINK_DEVICE_STICKY 0
#endif

#ifndef MALLOC_CAP_8BIT
#define MALLOC_CAP_8BIT 0
#endif
#ifndef MALLOC_CAP_SPIRAM
#define MALLOC_CAP_SPIRAM 0
#endif
#ifndef MALLOC_CAP_INTERNAL
#define MALLOC_CAP_INTERNAL 0
#endif

#ifndef ps_malloc
#define ps_malloc(bytes) std::malloc(bytes)
#endif

#ifdef __cplusplus
using StackType_t = uint8_t;
struct StaticTask_t {};
#else
typedef uint8_t StackType_t;
typedef struct StaticTask_t {} StaticTask_t;
#endif

#ifdef __cplusplus
template <typename... Args>
inline BaseType_t xTaskCreateWithCaps(Args...) { return pdPASS; }
template <typename... Args>
inline BaseType_t xTaskCreatePinnedToCoreWithCaps(Args...) { return pdPASS; }
template <typename Handle>
inline void vTaskDeleteWithCaps(Handle) {}
#endif

#ifndef SNTP_SYNC_STATUS_RESET
#define SNTP_SYNC_STATUS_RESET 0
#endif

#ifdef __cplusplus
inline void sntp_set_sync_status(int) {}
#endif

#ifndef vSemaphoreDelete
#define vSemaphoreDelete(sem) delete (sem)
#endif

#ifdef __cplusplus
inline TickType_t xTaskGetTickCount() {
  using namespace std::chrono;
  static const steady_clock::time_point start = steady_clock::now();
  return static_cast<TickType_t>(duration_cast<milliseconds>(steady_clock::now() - start).count());
}

inline uint32_t esp_random() {
  static uint32_t state = 0x9e3779b9U;
  state = state * 1664525U + 1013904223U + xTaskGetTickCount();
  return state;
}
#endif

#ifndef MANUAL_REFRESH
#define MANUAL_REFRESH FAST_REFRESH
#endif

#ifndef STRONG_FAST_REFRESH
#define STRONG_FAST_REFRESH FAST_REFRESH
#endif

#endif
