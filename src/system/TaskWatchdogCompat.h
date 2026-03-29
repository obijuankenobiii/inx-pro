#pragma once

// ESP-IDF 5.x reports an error when legacy code calls esp_task_wdt_reset() from
// a task that was never subscribed to the task watchdog. Several long-running
// paths intentionally call it opportunistically, so make those calls harmless
// while preserving the reset for subscribed tasks.

#ifdef __cplusplus

#include <esp_task_wdt.h>

inline esp_err_t inxTaskWdtReset() {
  if (esp_task_wdt_status(nullptr) != ESP_OK) return ESP_ERR_NOT_FOUND;
  return esp_task_wdt_reset();
}

#define esp_task_wdt_reset inxTaskWdtReset

#endif  // __cplusplus
