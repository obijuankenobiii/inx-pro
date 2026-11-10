#pragma once

enum sntp_sync_status_t { SNTP_SYNC_STATUS_RESET, SNTP_SYNC_STATUS_COMPLETED, SNTP_SYNC_STATUS_IN_PROGRESS };
enum sntp_sync_mode_t { SNTP_SYNC_MODE_IMMED, SNTP_SYNC_MODE_SMOOTH };
enum esp_sntp_operatingmode_t { ESP_SNTP_OPMODE_POLL };

bool esp_sntp_enabled();
void esp_sntp_stop();
void sntp_set_sync_status(sntp_sync_status_t status);
void esp_sntp_set_sync_mode(sntp_sync_mode_t mode);
void esp_sntp_setoperatingmode(esp_sntp_operatingmode_t mode);
void esp_sntp_setservername(int index, const char* name);
void esp_sntp_init();
sntp_sync_status_t sntp_get_sync_status();
