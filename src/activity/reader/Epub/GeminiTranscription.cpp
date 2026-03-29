#include "GeminiTranscription.h"

#include <Arduino.h>
#include <SDCardManager.h>
#include <WiFi.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <ArduinoJson.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>

#include "state/NetworkCredential.h"

extern "C" esp_err_t esp_crt_bundle_attach(void* conf);

namespace GeminiTranscription {
namespace {

constexpr char kKeyPath[] = "/.system/gemini_api_key.txt";
constexpr char kUploadUrl[] = "https://generativelanguage.googleapis.com/upload/v1beta/files";
constexpr char kGenerateBase[] = "https://generativelanguage.googleapis.com/v1beta/";
constexpr char kModel[] = "models/gemini-3.5-flash-lite";
constexpr char kMimeType[] = "audio/wav";
constexpr char kPrompt[] =
    "Generate a verbatim transcript of the speech in this audio. Respond with transcript text "
    "only. Do not add commentary or formatting.";
// esp_http_client's TLS handshake uses considerably more stack than a normal
// application task. Keep this separate from the reader task and give it room
// for mbedTLS without risking a stack-canary reset.
constexpr uint32_t kTaskStackWords = 16384;
constexpr size_t kIoBufferSize = 4096;

struct HttpResponse {
  int status = 0;
  std::string body;
  std::string uploadUrl;
  std::string error;
};

struct Job {
  std::string wavPath;
};

std::mutex gMutex;
bool gRunning = false;
Result gResult;

std::string trim(std::string value) {
  const auto notSpace = [](unsigned char c) { return !std::isspace(c); };
  value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
  value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
  return value;
}

std::string readApiKey() {
  FsFile file;
  if (!SdMan.openFileForRead("GEMINI", kKeyPath, file)) return {};
  std::string value;
  char buffer[128];
  while (true) {
    const int read = file.read(buffer, sizeof(buffer));
    if (read <= 0) break;
    value.append(buffer, static_cast<size_t>(read));
    if (value.size() > 512) break;
  }
  file.close();
  return trim(value);
}

esp_err_t httpEvent(esp_http_client_event_t* event) {
  if (!event) return ESP_FAIL;
  auto* response = static_cast<HttpResponse*>(event->user_data);
  if (!response) return ESP_OK;
  if (event->event_id == HTTP_EVENT_ON_DATA && event->data && event->data_len > 0) {
    response->body.append(static_cast<const char*>(event->data), static_cast<size_t>(event->data_len));
  } else if (event->event_id == HTTP_EVENT_ON_HEADER && event->header_key && event->header_value &&
             strcasecmp(event->header_key, "x-goog-upload-url") == 0) {
    response->uploadUrl = event->header_value;
  }
  return ESP_OK;
}

void readResponse(esp_http_client_handle_t client, HttpResponse& response) {
  char buffer[512];
  while (true) {
    const int read = esp_http_client_read(client, buffer, sizeof(buffer));
    if (read <= 0) break;
    response.body.append(buffer, static_cast<size_t>(read));
  }
}

bool checkHttp(const HttpResponse& response, const char* operation, std::string& error) {
  if (!response.error.empty()) {
    error = response.error;
    return false;
  }
  if (response.status < 200 || response.status >= 300) {
    error = std::string(operation) + " failed (HTTP " + std::to_string(response.status) + ")";
    if (!response.body.empty()) error += ": " + response.body.substr(0, 180);
    return false;
  }
  return true;
}

HttpResponse beginUpload(const std::string& apiKey, size_t bytes) {
  HttpResponse response;
  esp_http_client_config_t config = {};
  config.url = kUploadUrl;
  config.method = HTTP_METHOD_POST;
  config.crt_bundle_attach = esp_crt_bundle_attach;
  config.keep_alive_enable = false;
  config.buffer_size = 2048;
  config.buffer_size_tx = 1024;
  config.timeout_ms = 30000;
  config.event_handler = httpEvent;
  config.user_data = &response;
  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) {
    response.error = "Could not initialize Gemini upload";
    return response;
  }
  char length[32];
  snprintf(length, sizeof(length), "%u", static_cast<unsigned>(bytes));
  esp_http_client_set_header(client, "x-goog-api-key", apiKey.c_str());
  esp_http_client_set_header(client, "X-Goog-Upload-Protocol", "resumable");
  esp_http_client_set_header(client, "X-Goog-Upload-Command", "start");
  esp_http_client_set_header(client, "X-Goog-Upload-Header-Content-Length", length);
  esp_http_client_set_header(client, "X-Goog-Upload-Header-Content-Type", kMimeType);
  esp_http_client_set_header(client, "Content-Type", "application/json");
  const char metadata[] = "{\"file\":{\"display_name\":\"STICKY_HIGHLIGHT_NOTE\"}}";
  esp_http_client_set_post_field(client, metadata, sizeof(metadata) - 1);
  const esp_err_t err = esp_http_client_perform(client);
  response.status = esp_http_client_get_status_code(client);
  if (err != ESP_OK) response.error = esp_err_to_name(err);
  esp_http_client_cleanup(client);
  return response;
}

HttpResponse uploadFile(const std::string& apiKey, const std::string& uploadUrl, const std::string& path) {
  HttpResponse response;
  FsFile file;
  if (!SdMan.openFileForRead("GEMINI", path.c_str(), file)) {
    response.error = "Could not open recorded audio";
    return response;
  }
  const size_t bytes = file.size();
  esp_http_client_config_t config = {};
  config.url = uploadUrl.c_str();
  config.method = HTTP_METHOD_POST;
  config.crt_bundle_attach = esp_crt_bundle_attach;
  config.keep_alive_enable = false;
  config.buffer_size = 2048;
  config.buffer_size_tx = 1024;
  config.timeout_ms = 60000;
  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) {
    file.close();
    response.error = "Could not initialize Gemini audio upload";
    return response;
  }
  char length[32];
  snprintf(length, sizeof(length), "%u", static_cast<unsigned>(bytes));
  esp_http_client_set_header(client, "x-goog-api-key", apiKey.c_str());
  esp_http_client_set_header(client, "Content-Length", length);
  esp_http_client_set_header(client, "X-Goog-Upload-Offset", "0");
  esp_http_client_set_header(client, "X-Goog-Upload-Command", "upload, finalize");
  if (esp_http_client_open(client, bytes) != ESP_OK) {
    response.error = "Could not open Gemini audio upload";
    esp_http_client_cleanup(client);
    file.close();
    return response;
  }
  const std::unique_ptr<uint8_t[]> buffer(new (std::nothrow) uint8_t[kIoBufferSize]);
  if (!buffer) {
    response.error = "Could not allocate Gemini upload buffer";
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return response;
  }
  bool failed = false;
  while (true) {
    const int read = file.read(buffer.get(), kIoBufferSize);
    if (read <= 0) break;
    if (esp_http_client_write(client, reinterpret_cast<const char*>(buffer.get()), read) != read) {
      failed = true;
      break;
    }
    vTaskDelay(1);
  }
  file.close();
  if (failed) {
    response.error = "Could not stream audio to Gemini";
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return response;
  }
  esp_http_client_fetch_headers(client);
  response.status = esp_http_client_get_status_code(client);
  readResponse(client, response);
  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  return response;
}

HttpResponse generateTranscript(const std::string& apiKey, const std::string& fileUri) {
  HttpResponse response;
  const std::string url = std::string(kGenerateBase) + kModel + ":generateContent";
  esp_http_client_config_t config = {};
  config.url = url.c_str();
  config.method = HTTP_METHOD_POST;
  config.crt_bundle_attach = esp_crt_bundle_attach;
  config.keep_alive_enable = false;
  config.buffer_size = 2048;
  config.buffer_size_tx = 1024;
  config.timeout_ms = 60000;
  config.event_handler = httpEvent;
  config.user_data = &response;
  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) {
    response.error = "Could not initialize Gemini transcription";
    return response;
  }
  JsonDocument document;
  JsonArray contents = document["contents"].to<JsonArray>();
  JsonObject content = contents.add<JsonObject>();
  JsonArray parts = content["parts"].to<JsonArray>();
  JsonObject prompt = parts.add<JsonObject>();
  prompt["text"] = kPrompt;
  JsonObject audio = parts.add<JsonObject>();
  JsonObject fileData = audio["fileData"].to<JsonObject>();
  fileData["mimeType"] = kMimeType;
  fileData["fileUri"] = fileUri;
  std::string body;
  serializeJson(document, body);
  esp_http_client_set_header(client, "x-goog-api-key", apiKey.c_str());
  esp_http_client_set_header(client, "Content-Type", "application/json");
  esp_http_client_set_header(client, "Accept", "application/json");
  esp_http_client_set_post_field(client, body.c_str(), static_cast<int>(body.size()));
  const esp_err_t err = esp_http_client_perform(client);
  response.status = esp_http_client_get_status_code(client);
  if (err != ESP_OK) response.error = esp_err_to_name(err);
  esp_http_client_cleanup(client);
  return response;
}

void setResult(const Result& result) {
  std::lock_guard<std::mutex> lock(gMutex);
  gResult = result;
  gResult.finished = true;
  gRunning = false;
  INX_SERIAL.printf("[%lu] [GEMINI] finished success=%d error=%s transcript=%u\n", millis(),
                    result.success ? 1 : 0, result.error.empty() ? "none" : result.error.c_str(),
                    static_cast<unsigned>(result.transcript.size()));
}

bool ensureWifi(std::string& error) {
  if (WiFi.status() == WL_CONNECTED) return true;
  if (!WIFI_STORE.loadFromFile()) {
    error = "No saved Wi-Fi network";
    return false;
  }
  const auto& credentials = WIFI_STORE.getCredentials();
  if (credentials.empty()) {
    error = "No saved Wi-Fi network";
    return false;
  }
  WiFi.mode(WIFI_STA);
  for (const auto& credential : credentials) {
    if (credential.ssid.empty()) continue;
    WiFi.begin(credential.ssid.c_str(), credential.password.c_str());
    const unsigned long started = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - started < 15000UL) {
      vTaskDelay(pdMS_TO_TICKS(250));
    }
    if (WiFi.status() == WL_CONNECTED) return true;
    WiFi.disconnect(false);
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  error = "Wi-Fi connection failed";
  return false;
}

void worker(void* raw) {
  std::unique_ptr<Job> job(static_cast<Job*>(raw));
  Result result;
  const std::string apiKey = readApiKey();
  if (apiKey.empty()) {
    result.error = "Gemini API key is not configured";
    setResult(result);
    vTaskDelete(nullptr);
    return;
  }
  std::string wifiError;
  if (!ensureWifi(wifiError)) {
    result.error = wifiError;
    setResult(result);
    vTaskDelete(nullptr);
    return;
  }
  IPAddress resolved;
  if (WiFi.hostByName("generativelanguage.googleapis.com", resolved) != 1) {
    result.error = "DNS failed for Gemini; Wi-Fi has no internet";
    INX_SERIAL.printf("[%lu] [GEMINI] DNS failed status=%d ip=%s\n", millis(), WiFi.status(),
                      WiFi.localIP().toString().c_str());
    setResult(result);
    vTaskDelete(nullptr);
    return;
  }
  INX_SERIAL.printf("[%lu] [GEMINI] network ready ssid=%s ip=%s dns=%s\n", millis(),
                    WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), resolved.toString().c_str());
  FsFile file;
  if (!SdMan.openFileForRead("GEMINI", job->wavPath.c_str(), file)) {
    result.error = "Could not open recorded audio";
    setResult(result);
    vTaskDelete(nullptr);
    return;
  }
  const size_t bytes = file.size();
  file.close();
  std::string error;
  const HttpResponse startResponse = beginUpload(apiKey, bytes);
  if (!checkHttp(startResponse, "Gemini upload start", error) || startResponse.uploadUrl.empty()) {
    result.error = startResponse.uploadUrl.empty() && error.empty() ? "Gemini did not return an upload URL" : error;
    setResult(result);
    vTaskDelete(nullptr);
    return;
  }
  const HttpResponse uploadResponse = uploadFile(apiKey, startResponse.uploadUrl, job->wavPath);
  if (!checkHttp(uploadResponse, "Gemini audio upload", error)) {
    result.error = error;
    setResult(result);
    vTaskDelete(nullptr);
    return;
  }
  JsonDocument uploadDoc;
  if (deserializeJson(uploadDoc, uploadResponse.body) != DeserializationError::Ok) {
    result.error = "Gemini returned invalid upload data";
    setResult(result);
    vTaskDelete(nullptr);
    return;
  }
  const char* fileUri = uploadDoc["file"]["uri"] | "";
  if (!fileUri || !*fileUri) {
    result.error = "Gemini upload did not return a file URI";
    setResult(result);
    vTaskDelete(nullptr);
    return;
  }
  const HttpResponse transcriptResponse = generateTranscript(apiKey, fileUri);
  if (!checkHttp(transcriptResponse, "Gemini transcription", error)) {
    result.error = error;
    setResult(result);
    vTaskDelete(nullptr);
    return;
  }
  JsonDocument transcriptDoc;
  if (deserializeJson(transcriptDoc, transcriptResponse.body) != DeserializationError::Ok) {
    result.error = "Gemini returned invalid transcription data";
    setResult(result);
    vTaskDelete(nullptr);
    return;
  }
  const char* text = transcriptDoc["candidates"][0]["content"]["parts"][0]["text"] | "";
  if (!text || !*text) {
    result.error = "Gemini returned an empty transcript";
    setResult(result);
    vTaskDelete(nullptr);
    return;
  }
  result.success = true;
  result.transcript = text;
  setResult(result);
  vTaskDelete(nullptr);
}

}  // namespace

bool start(const std::string& wavPath) {
  std::lock_guard<std::mutex> lock(gMutex);
  if (gRunning || wavPath.empty()) return false;
  auto* job = new (std::nothrow) Job{wavPath};
  if (!job) return false;
  gRunning = true;
  gResult = {};
  TaskHandle_t handle = nullptr;
  if (xTaskCreatePinnedToCore(worker, "gemini_note", kTaskStackWords, job, 1, &handle, 1) != pdPASS) {
    delete job;
    gRunning = false;
    return false;
  }
  return true;
}

Result poll() {
  std::lock_guard<std::mutex> lock(gMutex);
  const Result result = gResult;
  if (result.finished) gResult = {};
  return result;
}

bool configured() { return !readApiKey().empty(); }

bool saveApiKey(const std::string& apiKey) {
  const std::string value = trim(apiKey);
  if (value.empty()) return clearApiKey();
  SdMan.mkdir("/.system");
  FsFile file;
  if (!SdMan.openFileForWrite("GEMINI", kKeyPath, file)) return false;
  const size_t written = file.write(value.data(), value.size());
  file.close();
  return written == value.size();
}

bool clearApiKey() {
  if (!SdMan.exists(kKeyPath)) return true;
  return SdMan.remove(kKeyPath);
}

std::string apiKeyLast4() {
  const std::string key = readApiKey();
  return key.size() > 4 ? key.substr(key.size() - 4) : std::string();
}

}  // namespace GeminiTranscription
