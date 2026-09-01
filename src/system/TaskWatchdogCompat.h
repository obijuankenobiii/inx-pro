#pragma once

#ifdef __cplusplus

#include <esp_task_wdt.h>

inline esp_err_t inxTaskWdtReset() {
  if (esp_task_wdt_status(nullptr) != ESP_OK) return ESP_ERR_NOT_FOUND;
  return esp_task_wdt_reset();
}

#define esp_task_wdt_reset inxTaskWdtReset

#endif
