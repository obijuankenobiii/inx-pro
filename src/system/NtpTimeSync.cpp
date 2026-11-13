#include "NtpTimeSync.h"

#include <Arduino.h>
#include <esp_sntp.h>

bool syncSystemClockWithNtp(time_t& utcNow, const uint32_t timeoutMs) {
  if (esp_sntp_enabled()) esp_sntp_stop();
  sntp_set_sync_status(SNTP_SYNC_STATUS_RESET);
#ifndef SIMULATOR
  esp_sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
#endif
  esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, "pool.ntp.org");
  esp_sntp_setservername(1, "time.google.com");
  esp_sntp_init();

  const uint32_t start = millis();
  bool completed = false;
  while (true) {
    // This getter consumes COMPLETED; read it once and retain the result.
    if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
      completed = true;
      break;
    }
    if (static_cast<uint32_t>(millis()) - start >= timeoutMs) break;
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  esp_sntp_stop();

  if (!completed) return false;
  const time_t synced = time(nullptr);
  if (synced < 1704067200) return false;
  utcNow = synced;
  return true;
}
