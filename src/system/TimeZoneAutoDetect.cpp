#include "TimeZoneAutoDetect.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HardwareSerial.h>

#include <cstring>
#include <string>

#include "state/SystemSetting.h"

#ifndef INX_SIMULATOR_WEB_ONLY
#include "esp_http_client.h"
#endif

namespace {
constexpr char kTimeZoneLookupUrl[] = "http://ip-api.com/json/?fields=status,offset,timezone";
constexpr int kHttpTimeoutMs = 5000;

#ifndef INX_SIMULATOR_WEB_ONLY
struct ResponseContext {
  std::string body;
};

esp_err_t onHttpEvent(esp_http_client_event_t* event) {
  if (event->event_id == HTTP_EVENT_ON_DATA && event->data != nullptr && event->data_len > 0) {
    auto* context = static_cast<ResponseContext*>(event->user_data);
    if (context) {
      context->body.append(static_cast<const char*>(event->data), event->data_len);
    }
  }
  return ESP_OK;
}
#endif
}  // namespace

bool autoDetectTimeZone() {
#ifdef INX_SIMULATOR_WEB_ONLY
  return false;
#else
  ResponseContext context;
  esp_http_client_config_t config = {};
  config.url = kTimeZoneLookupUrl;
  config.event_handler = onHttpEvent;
  config.user_data = &context;
  config.timeout_ms = kHttpTimeoutMs;
  config.keep_alive_enable = false;
  config.buffer_size = 512;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) {
    INX_SERIAL.printf("[%lu] [TIMEZONE] HTTP client init failed\n", millis());
    return false;
  }

  esp_http_client_set_method(client, HTTP_METHOD_GET);
  const esp_err_t requestResult = esp_http_client_perform(client);
  const int statusCode = esp_http_client_get_status_code(client);
  esp_http_client_cleanup(client);

  if (requestResult != ESP_OK || statusCode != 200) {
    INX_SERIAL.printf("[%lu] [TIMEZONE] lookup failed result=%s status=%d\n", millis(),
                      esp_err_to_name(requestResult), statusCode);
    return false;
  }

  JsonDocument document;
  if (deserializeJson(document, context.body)) {
    INX_SERIAL.printf("[%lu] [TIMEZONE] invalid lookup response\n", millis());
    return false;
  }

  const char* result = document["status"] | "fail";
  const int offsetSeconds = document["offset"] | 0;
  const char* timeZone = document["timezone"] | "";
  if (strcmp(result, "success") != 0 || offsetSeconds < -43200 || offsetSeconds > 50400 ||
      offsetSeconds % (15 * 60) != 0) {
    INX_SERIAL.printf("[%lu] [TIMEZONE] lookup returned no valid offset\n", millis());
    return false;
  }

  const int quarterOffset = offsetSeconds / (15 * 60) + 48;
  if (quarterOffset < 0 || quarterOffset > 104) {
    return false;
  }

  const uint8_t detectedOffset = static_cast<uint8_t>(quarterOffset);
  if (SETTINGS.timeZoneQuarterOffset != detectedOffset) {
    SETTINGS.timeZoneQuarterOffset = detectedOffset;
    SETTINGS.saveToFile();
  }
  INX_SERIAL.printf("[%lu] [TIMEZONE] detected %s UTC offset=%d minutes\n", millis(), timeZone,
                    SETTINGS.getTimeZoneOffsetMinutes());
  return true;
#endif
}
