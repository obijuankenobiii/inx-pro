/**
 * @file TimeSyncActivity.cpp
 * @brief WiFi/NTP time synchronization for the Sticky RTC clock.
 */

#include "TimeSyncActivity.h"

#include <ArduinoJson.h>
#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <WiFi.h>
#ifndef INX_SIMULATOR_WEB_ONLY
#include <esp_http_client.h>
#endif
#include <esp_sntp.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <cstring>

#include "activity/page/SubPage.h"
#include "activity/network/WifiSelectionActivity.h"
#include "activity/page/components/global/Button.h"
#include "activity/page/components/search/SearchText.h"
#include "images/LibraryFilterRight.h"
#include "images/Search.h"
#include "images/Shift.h"
#include "state/SystemSetting.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "system/TimeZoneAutoDetect.h"
#include "system/UiLayout.h"

namespace {
constexpr int NTP_TIMEOUT_MS = 8000;
constexpr int HTTP_TIMEOUT_MS = 8000;
constexpr int TIMEZONE_ROW_HEIGHT = UiLayout::LIST_ITEM_HEIGHT;
constexpr int TIMEZONE_SIDE_MARGIN = 20;
constexpr int TIMEZONE_BOTTOM_MARGIN = 44;
constexpr int TIMEZONE_CARET_SIZE = 40;
constexpr int TIMEZONE_KEYBOARD_CONTROL_HEIGHT = UiLayout::LIST_ITEM_HEIGHT;
constexpr int TIMEZONE_KEYBOARD_CONTROL_WIDTH = 112;
constexpr int TIMEZONE_KEYBOARD_CONTROL_GAP = 10;
constexpr char TIMEZONE_LIST_URL[] = "https://time.now/developer/api/timezone";
constexpr char TIMEZONE_URL_PREFIX[] = "https://time.now/developer/api/timezone/";

#ifndef INX_SIMULATOR_WEB_ONLY
extern "C" esp_err_t esp_crt_bundle_attach(void* conf);

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

bool performHttpGet(const std::string& url, ResponseContext& response) {
  esp_http_client_config_t config = {};
  config.url = url.c_str();
  config.event_handler = onHttpEvent;
  config.user_data = &response;
  config.timeout_ms = HTTP_TIMEOUT_MS;
  config.crt_bundle_attach = esp_crt_bundle_attach;
  config.keep_alive_enable = false;
  config.disable_auto_redirect = true;
  config.buffer_size = 2048;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) {
    return false;
  }

  esp_http_client_set_method(client, HTTP_METHOD_GET);
  const esp_err_t result = esp_http_client_perform(client);
  const int statusCode = esp_http_client_get_status_code(client);
  if (result != ESP_OK || statusCode != 200) {
    INX_SERIAL.printf("[%lu] [TIMEZONE] HTTP GET failed result=%s status=%d\n", millis(), esp_err_to_name(result),
                      statusCode);
  }
  esp_http_client_cleanup(client);
  return result == ESP_OK && statusCode == 200;
}
#endif

bool parseUtcOffset(const char* value, int& quarterOffset) {
  if (value == nullptr || (value[0] != '+' && value[0] != '-') || value[3] != ':' || value[6] != '\0') {
    return false;
  }
  if (value[1] < '0' || value[1] > '9' || value[2] < '0' || value[2] > '9' || value[4] < '0' ||
      value[4] > '9' || value[5] < '0' || value[5] > '9') {
    return false;
  }
  const int hours = (value[1] - '0') * 10 + (value[2] - '0');
  const int minutes = (value[4] - '0') * 10 + (value[5] - '0');
  const int totalMinutes = (value[0] == '-' ? -1 : 1) * (hours * 60 + minutes);
  if (totalMinutes < -12 * 60 || totalMinutes > 14 * 60 || totalMinutes % 15 != 0) {
    return false;
  }
  quarterOffset = totalMinutes / 15 + 48;
  return quarterOffset >= 0 && quarterOffset <= 104;
}

int timezoneKeyboardBottom(const GfxRenderer& renderer, const bool collapsed) {
  return collapsed ? renderer.getScreenHeight() - TIMEZONE_KEYBOARD_CONTROL_HEIGHT : renderer.getScreenHeight();
}

int timezoneKeyboardTop(const GfxRenderer& renderer, const SearchKeyboard& keyboard, const bool collapsed) {
  const int bottom = timezoneKeyboardBottom(renderer, collapsed);
  return collapsed ? bottom : bottom - keyboard.height(renderer);
}

int timezoneListTop() { return SearchText::top() + SearchText::height + 10; }

int timezoneVisibleRows(const GfxRenderer& renderer, const SearchKeyboard& keyboard, const bool collapsed) {
  const int listBottom = collapsed ? renderer.getScreenHeight() : timezoneKeyboardTop(renderer, keyboard, false);
  return std::max(1, (listBottom - timezoneListTop()) / TIMEZONE_ROW_HEIGHT);
}

ButtonBounds timezoneCaretBounds(const GfxRenderer& renderer) {
  return {renderer.getScreenWidth() - TIMEZONE_SIDE_MARGIN - TIMEZONE_CARET_SIZE,
          renderer.getScreenHeight() - TIMEZONE_BOTTOM_MARGIN + 2, TIMEZONE_CARET_SIZE, TIMEZONE_CARET_SIZE};
}

ButtonBounds timezoneKeyboardControlBounds(const GfxRenderer& renderer) {
  const ButtonBounds caret = timezoneCaretBounds(renderer);
  return {caret.x - TIMEZONE_KEYBOARD_CONTROL_GAP - TIMEZONE_KEYBOARD_CONTROL_WIDTH,
          renderer.getScreenHeight() - TIMEZONE_KEYBOARD_CONTROL_HEIGHT, TIMEZONE_KEYBOARD_CONTROL_WIDTH,
          TIMEZONE_KEYBOARD_CONTROL_HEIGHT};
}

bool contains(const ButtonBounds& bounds, const int x, const int y) {
  return x >= bounds.x && x < bounds.x + bounds.width && y >= bounds.y && y < bounds.y + bounds.height;
}

void drawTimezoneScrollBar(const GfxRenderer& renderer, const int x, const int y, const int height, const int total,
                           const int visible, const int offset) {
  if (total <= visible || height <= 0) return;

  constexpr int width = 3;
  const int maxOffset = std::max(1, total - visible);
  const int thumbHeight = std::max(14, height * visible / total);
  const int thumbTravel = std::max(1, height - thumbHeight);
  const int thumbY = y + offset * thumbTravel / maxOffset;
  renderer.rectangle.fill(x, y, width, height, static_cast<int>(GfxRenderer::FillTone::Gray), true);
  renderer.rectangle.fill(x, thumbY, width, thumbHeight, static_cast<int>(GfxRenderer::FillTone::Ink), true);
}

void renderTimezoneKeyboardControl(const GfxRenderer& renderer, const int y, const int height) {
  const ButtonBounds bounds = timezoneKeyboardControlBounds(renderer);
  const int x = bounds.x;
  const int width = bounds.width;
  renderer.rectangle.render(x, y, width, height, true);

  constexpr int iconSize = 30;
  constexpr int gap = 8;
  const int font = systemFontId();
  const int labelWidth = renderer.text.getWidth(font, "ABC");
  const int textY = y + (height - renderer.text.getLineHeight(font)) / 2;
  const int contentWidth = labelWidth + gap + iconSize;
  const int contentX = x + (width - contentWidth) / 2;
  renderer.text.render(font, contentX, textY, "ABC", true, EpdFontFamily::REGULAR);
  renderer.bitmap.iconScaled(LibraryFilterRight, contentX + labelWidth + gap, y + (height - iconSize) / 2,
                             iconSize, iconSize, iconSize, iconSize, BitmapRender::Orientation::Rotate270CW);
}

uint8_t weekdayFromTm(const tm& t) { return static_cast<uint8_t>(t.tm_wday == 0 ? 7 : t.tm_wday); }
}

void TimeSyncActivity::onEnter() {
  ActivityWithSubactivity::onEnter();
  beginWifiOrSync();
}

void TimeSyncActivity::loop() {
  if (subActivity) {
    subActivity->loop();
    return;
  }

  const bool selectingTimezone = state == State::SELECTING_TIMEZONE;
  if (SubPage::closeInput(renderer, mappedInput, onBack, !selectingTimezone)) return;

  if (selectingTimezone) {
    const int listTop = timezoneListTop();
    const int searchTop = SearchText::top();
    const int searchButtonX = renderer.getScreenWidth() - TIMEZONE_SIDE_MARGIN - TIMEZONE_CARET_SIZE;
    const int visibleRows = timezoneVisibleRows(renderer, timezoneKeyboard, timezoneKeyboardCollapsed);
    const int maxScroll = std::max(0, static_cast<int>(filteredTimezoneOptions.size()) - visibleRows);

    if (!timezoneKeyboardCollapsed) {
      float tapNx = 0.0f;
      float tapNy = 0.0f;
      if (mappedInput.hasTouch() && mappedInput.wasTouchTapInScreen(renderer, tapNx, tapNy)) {
        const int tapX = static_cast<int>(tapNx * renderer.getScreenWidth());
        const int tapY = static_cast<int>(tapNy * renderer.getScreenHeight());
        const int keyboardTop = timezoneKeyboardTop(renderer, timezoneKeyboard, false);
        if (tapY >= listTop && tapY < keyboardTop) {
          const int rowsAboveKeyboard = std::max(1, (keyboardTop - listTop) / TIMEZONE_ROW_HEIGHT);
          const int row = (tapY - listTop) / TIMEZONE_ROW_HEIGHT;
          const int index = timezoneScrollOffset + row;
          if (row >= 0 && row < rowsAboveKeyboard && index >= 0 &&
              index < static_cast<int>(filteredTimezoneOptions.size())) {
            selectTimezone(index);
            return;
          }
        }
        const std::string previousQuery = timezoneQuery;
        if (timezoneKeyboard.tap(renderer, timezoneKeyboardTop(renderer, timezoneKeyboard, false),
                                 timezoneKeyboardBottom(renderer, false), tapX, tapY, timezoneQuery)) {
          if (timezoneKeyboard.consumeCollapse()) {
            timezoneKeyboardCollapsed = true;
          }
          if (timezoneQuery != previousQuery) {
            applyTimezoneFilter();
          }
          if (timezoneKeyboard.consumeGo()) {
            timezoneKeyboardCollapsed = true;
          }
          render();
          return;
        }
      }
      return;
    }

    if (mappedInput.hasTouch() && mappedInput.wasTouchSwipeUpForRenderer(renderer)) {
      timezoneScrollOffset = std::min(maxScroll, timezoneScrollOffset + std::max(1, visibleRows - 1));
      timezoneSelection = std::min(std::max(0, static_cast<int>(filteredTimezoneOptions.size()) - 1),
                                   timezoneScrollOffset);
      render();
      return;
    }
    if (mappedInput.hasTouch() && mappedInput.wasTouchSwipeDownForRenderer(renderer)) {
      timezoneScrollOffset = std::max(0, timezoneScrollOffset - std::max(1, visibleRows - 1));
      timezoneSelection = std::min(std::max(0, static_cast<int>(filteredTimezoneOptions.size()) - 1),
                                   timezoneScrollOffset);
      render();
      return;
    }

    float tapNx = 0.0f;
    float tapNy = 0.0f;
    if (mappedInput.wasTouchTapInScreen(renderer, tapNx, tapNy)) {
      const int y = static_cast<int>(tapNy * renderer.getScreenHeight());
      const int x = static_cast<int>(tapNx * renderer.getScreenWidth());
      if (y >= searchTop && y < searchTop + SearchText::height && x < searchButtonX) {
        timezoneKeyboardCollapsed = false;
        render();
        return;
      }
      if (y >= searchTop && y < searchTop + SearchText::height && x >= searchButtonX &&
          x < searchButtonX + TIMEZONE_CARET_SIZE) {
        timezoneKeyboardCollapsed = false;
        render();
        return;
      }
      if (maxScroll > 0 && contains(timezoneCaretBounds(renderer), x, y)) {
        if (timezoneScrollOffset >= maxScroll) {
          timezoneScrollOffset = std::max(0, timezoneScrollOffset - std::max(1, visibleRows - 1));
        } else {
          timezoneScrollOffset = std::min(maxScroll, timezoneScrollOffset + std::max(1, visibleRows - 1));
        }
        timezoneSelection = std::max(0, std::min(static_cast<int>(filteredTimezoneOptions.size()) - 1,
                                                 timezoneScrollOffset));
        render();
        return;
      }
      if (contains(timezoneKeyboardControlBounds(renderer), x, y)) {
        timezoneKeyboardCollapsed = false;
        render();
        return;
      }
      if (y >= listTop) {
        const int row = (y - listTop) / TIMEZONE_ROW_HEIGHT;
        const int index = timezoneScrollOffset + row;
        if (row >= 0 && row < visibleRows && index >= 0 && index < static_cast<int>(filteredTimezoneOptions.size())) {
          selectTimezone(index);
        }
      }
      return;
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
      moveTimezoneSelection(-1);
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
      moveTimezoneSelection(1);
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      selectTimezone(timezoneSelection);
      return;
    }
  }

  if ((state == State::DONE || state == State::FAILED) && mappedInput.hasTouch()) {
    float tapNx = 0.0f;
    float tapNy = 0.0f;
    if (mappedInput.wasTouchTapInScreen(renderer, tapNx, tapNy)) {
      onBack();
      return;
    }
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
      mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    onBack();
  }
}

void TimeSyncActivity::render() {
  renderer.clearScreen();
  const int h = renderer.getScreenHeight();

  const int contentTop =
      SubPage::header(renderer, state == State::SELECTING_TIMEZONE ? "Select timezone" : "Sync time");
  const int centerY = contentTop + (h - contentTop - 80) / 2;
  const int titleY = centerY - 36;
  const int bodyY = centerY;

  if (state == State::SELECTING_TIMEZONE) {
    const int listTop = timezoneListTop();
    const int visibleRows = timezoneVisibleRows(renderer, timezoneKeyboard, timezoneKeyboardCollapsed);
    SearchText::render(renderer, timezoneQuery, "Search timezone");
    renderer.bitmap.icon(Search, renderer.getScreenWidth() - TIMEZONE_SIDE_MARGIN - TIMEZONE_CARET_SIZE,
                         SearchText::top() + (SearchText::height - TIMEZONE_CARET_SIZE) / 2, TIMEZONE_CARET_SIZE,
                         TIMEZONE_CARET_SIZE);

    const int maxScroll = std::max(0, static_cast<int>(filteredTimezoneOptions.size()) - visibleRows);
    timezoneScrollOffset = std::max(0, std::min(timezoneScrollOffset, maxScroll));
    const int first = timezoneScrollOffset;
    const int last = std::min(static_cast<int>(filteredTimezoneOptions.size()), first + visibleRows);
    for (int index = first; index < last; ++index) {
      const int row = index - first;
      const int y = listTop + row * TIMEZONE_ROW_HEIGHT;
      const int textY = y + (TIMEZONE_ROW_HEIGHT - renderer.text.getLineHeight(systemFontId())) / 2;
      renderer.text.render(systemFontId(), TIMEZONE_SIDE_MARGIN, textY, filteredTimezoneOptions[index].c_str(), true,
                           EpdFontFamily::REGULAR);
      if (index + 1 < last) {
        renderer.line.render(0, y + TIMEZONE_ROW_HEIGHT - 1, renderer.getScreenWidth(),
                             y + TIMEZONE_ROW_HEIGHT - 1, true, LineRender::Style::Dotted);
      }
    }
    if (filteredTimezoneOptions.empty() && !timezoneQuery.empty()) {
      renderer.text.centered(systemFontId(), listTop + 24, "No matching timezones", true, EpdFontFamily::BOLD);
    }

    if (timezoneKeyboardCollapsed) {
      renderTimezoneKeyboardControl(renderer, renderer.getScreenHeight() - TIMEZONE_KEYBOARD_CONTROL_HEIGHT,
                                    TIMEZONE_KEYBOARD_CONTROL_HEIGHT);
    } else {
      timezoneKeyboard.render(renderer, timezoneKeyboardTop(renderer, timezoneKeyboard, false),
                              timezoneKeyboardBottom(renderer, false));
    }
    if (timezoneKeyboardCollapsed && maxScroll > 0) {
      drawTimezoneScrollBar(renderer, renderer.getScreenWidth() - 8, listTop, visibleRows * TIMEZONE_ROW_HEIGHT,
                            static_cast<int>(filteredTimezoneOptions.size()), visibleRows, timezoneScrollOffset);
      const auto caretOrientation = timezoneScrollOffset >= maxScroll ? BitmapRender::Orientation::Rotate270CW
                                                                        : BitmapRender::Orientation::Rotate90CW;
      const ButtonBounds caretBounds = timezoneCaretBounds(renderer);
      renderer.bitmap.iconScaled(LibraryFilterRight, caretBounds.x, caretBounds.y, 30, 30, TIMEZONE_CARET_SIZE,
                                 TIMEZONE_CARET_SIZE, caretOrientation);
    }
    const auto labels = mappedInput.mapLabels("\xC2\xAB Back", "Select", "Up", "Down");
    renderer.displayBuffer();
    return;
  }

  const char* title = "Select network";
  if (state == State::LOADING_TIMEZONES) {
    title = "Loading timezones";
  }
  if (state == State::DONE) {
    title = "Time synced";
  } else if (state == State::FAILED) {
    title = "Time sync failed";
  }

  renderer.text.centered(systemFontId(), titleY, title, true, EpdFontFamily::BOLD);
  renderer.text.centered(systemFontId(), bodyY, message.c_str(), true);

  char tz[16];
  SETTINGS.formatTimeZone(tz, sizeof(tz));
  renderer.text.centered(MONTSERRAT_8_FONT_ID, bodyY + 32, tz, true);

  if (state == State::DONE || state == State::FAILED) {
    const auto labels = mappedInput.mapLabels("\xC2\xAB Back", "Done", "", "");
  }

  renderer.displayBuffer();
}

void TimeSyncActivity::beginWifiOrSync() {
  state = State::CONNECTING;
  message = "Select a WiFi network";
  render();

  if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
    startTimezoneSelection();
    return;
  }

  enterNewActivity(
      new WifiSelectionActivity(renderer, mappedInput, [this](const bool connected) { onWifiComplete(connected); }));
}

void TimeSyncActivity::onWifiComplete(const bool connected) {
  exitActivity();

  if (!connected) {
    state = State::FAILED;
    message = "WiFi was not connected";
    render();
    return;
  }

  startTimezoneSelection();
}

void TimeSyncActivity::startTimezoneSelection() {
  state = State::LOADING_TIMEZONES;
  message = "Downloading complete timezone list";
  render();
  if (!loadTimezoneOptions()) {
    state = State::FAILED;
    message = "Could not load timezones";
    wifiOff();
    render();
    return;
  }
  state = State::SELECTING_TIMEZONE;
  timezoneScrollOffset = 0;
  timezoneSelection = 0;
  timezoneQuery.clear();
  timezoneKeyboard = SearchKeyboard();
  timezoneKeyboardCollapsed = true;
  if (!SETTINGS.timeZoneId[0]) {
    timezoneOptions.insert(timezoneOptions.begin(), "Automatic (network)");
  } else {
    const std::string selectedZone = SETTINGS.timeZoneId;
    const auto found = std::find(timezoneOptions.begin(), timezoneOptions.end(), selectedZone);
    if (found != timezoneOptions.end()) {
      timezoneSelection = static_cast<int>(std::distance(timezoneOptions.begin(), found)) + 1;
    } else {
      timezoneSelection = 0;
    }
    timezoneOptions.insert(timezoneOptions.begin(), "Automatic (network)");
  }
  applyTimezoneFilter();
  if (SETTINGS.timeZoneId[0]) {
    const auto found = std::find(filteredTimezoneOptions.begin(), filteredTimezoneOptions.end(), SETTINGS.timeZoneId);
    if (found != filteredTimezoneOptions.end()) {
      timezoneSelection = static_cast<int>(std::distance(filteredTimezoneOptions.begin(), found));
    }
  }
  render();
}

bool TimeSyncActivity::loadTimezoneOptions() {
  timezoneOptions.clear();
  filteredTimezoneOptions.clear();
#ifdef INX_SIMULATOR_WEB_ONLY
  return false;
#else
  ResponseContext response;
  if (!performHttpGet(TIMEZONE_LIST_URL, response)) {
    INX_SERIAL.printf("[%lu] [TIMEZONE] list request failed\n", millis());
    return false;
  }

  JsonDocument document;
  if (deserializeJson(document, response.body)) {
    INX_SERIAL.printf("[%lu] [TIMEZONE] list response was not valid JSON\n", millis());
    return false;
  }
  JsonArray zones = document.as<JsonArray>();
  for (JsonVariant zone : zones) {
    const char* name = zone.as<const char*>();
    if (name != nullptr && name[0] != '\0') {
      timezoneOptions.emplace_back(name);
    }
  }
  INX_SERIAL.printf("[%lu] [TIMEZONE] loaded %u zones\n", millis(), static_cast<unsigned>(timezoneOptions.size()));
  return !timezoneOptions.empty();
#endif
}

void TimeSyncActivity::applyTimezoneFilter() {
  filteredTimezoneOptions.clear();

  std::string query = timezoneQuery;
  std::transform(query.begin(), query.end(), query.begin(),
                 [](const unsigned char value) { return static_cast<char>(std::tolower(value)); });
  for (const std::string& option : timezoneOptions) {
    std::string normalized = option;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](const unsigned char value) { return static_cast<char>(std::tolower(value)); });
    if (query.empty() || normalized.find(query) != std::string::npos) {
      filteredTimezoneOptions.push_back(option);
    }
  }
  timezoneScrollOffset = 0;
  timezoneSelection = 0;
}

bool TimeSyncActivity::loadTimezoneOffset(const std::string& timezone, uint8_t& quarterOffset) const {
#ifdef INX_SIMULATOR_WEB_ONLY
  (void)timezone;
  (void)quarterOffset;
  return false;
#else
  ResponseContext response;
  if (!performHttpGet(std::string(TIMEZONE_URL_PREFIX) + timezone, response)) {
    INX_SERIAL.printf("[%lu] [TIMEZONE] zone lookup failed: %s\n", millis(), timezone.c_str());
    return false;
  }
  JsonDocument document;
  if (deserializeJson(document, response.body)) {
    return false;
  }
  int encoded = 0;
  if (!parseUtcOffset(document["utc_offset"] | "", encoded)) {
    return false;
  }
  quarterOffset = static_cast<uint8_t>(encoded);
  return true;
#endif
}

void TimeSyncActivity::moveTimezoneSelection(const int delta) {
  if (filteredTimezoneOptions.empty()) return;
  const int last = static_cast<int>(filteredTimezoneOptions.size()) - 1;
  timezoneSelection = std::max(0, std::min(last, timezoneSelection + delta));
  const int visibleRows = timezoneVisibleRows(renderer, timezoneKeyboard, true);
  const int maxScroll = std::max(0, static_cast<int>(filteredTimezoneOptions.size()) - visibleRows);
  if (timezoneSelection < timezoneScrollOffset) timezoneScrollOffset = timezoneSelection;
  if (timezoneSelection >= timezoneScrollOffset + visibleRows) timezoneScrollOffset = timezoneSelection - visibleRows + 1;
  timezoneScrollOffset = std::max(0, std::min(maxScroll, timezoneScrollOffset));
  render();
}

void TimeSyncActivity::selectTimezone(const int index) {
  if (index < 0 || index >= static_cast<int>(filteredTimezoneOptions.size())) return;
  const std::string selected = filteredTimezoneOptions[static_cast<size_t>(index)];
  if (selected == "Automatic (network)") {
    SETTINGS.timeZoneId[0] = '\0';
    SETTINGS.timeZoneAutoDetectEnabled = 1;
    SETTINGS.saveToFile();
    performSync();
    return;
  }

  state = State::SYNCING;
  message = "Applying timezone";
  render();
  uint8_t quarterOffset = SETTINGS.timeZoneQuarterOffset;
  if (!loadTimezoneOffset(selected, quarterOffset)) {
    state = State::FAILED;
    message = "Could not apply timezone";
    wifiOff();
    render();
    return;
  }
  std::strncpy(SETTINGS.timeZoneId, selected.c_str(), sizeof(SETTINGS.timeZoneId) - 1);
  SETTINGS.timeZoneId[sizeof(SETTINGS.timeZoneId) - 1] = '\0';
  SETTINGS.timeZoneQuarterOffset = quarterOffset;
  SETTINGS.timeZoneAutoDetectEnabled = 0;
  SETTINGS.saveToFile();
  performSync();
}

void TimeSyncActivity::performSync() {
  state = State::SYNCING;
  message = "Syncing from pool.ntp.org";
  render();

  if (esp_sntp_enabled()) {
    esp_sntp_stop();
  }
  sntp_set_sync_status(SNTP_SYNC_STATUS_RESET);
  esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, "pool.ntp.org");
  esp_sntp_setservername(1, "time.google.com");
  esp_sntp_init();

  const uint32_t start = millis();
  while (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED && millis() - start < NTP_TIMEOUT_MS) {
    vTaskDelay(pdMS_TO_TICKS(100));
  }

  const time_t utcNow = time(nullptr);
  if (utcNow < 1704067200) {
    state = State::FAILED;
    message = "NTP did not return time";
    wifiOff();
    render();
    return;
  }

  autoDetectTimeZone();

  const time_t localEpoch = utcNow + SETTINGS.getTimeZoneOffsetMinutes() * 60;
  tm local{};
  if (gmtime_r(&localEpoch, &local) == nullptr) {
    state = State::FAILED;
    message = "Could not apply timezone";
    wifiOff();
    render();
    return;
  }

  struct SyncedDateTime {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t weekday;
  } dt;
  dt.year = static_cast<uint16_t>(local.tm_year + 1900);
  dt.month = static_cast<uint8_t>(local.tm_mon + 1);
  dt.day = static_cast<uint8_t>(local.tm_mday);
  dt.hour = static_cast<uint8_t>(local.tm_hour);
  dt.minute = static_cast<uint8_t>(local.tm_min);
  dt.second = static_cast<uint8_t>(local.tm_sec);
  dt.weekday = weekdayFromTm(local);

  char buffer[40];
#ifndef SIMULATOR
  HalGPIO::DateTime rtcDateTime;
  rtcDateTime.year = dt.year;
  rtcDateTime.month = dt.month;
  rtcDateTime.day = dt.day;
  rtcDateTime.hour = dt.hour;
  rtcDateTime.minute = dt.minute;
  rtcDateTime.second = dt.second;
  rtcDateTime.weekday = dt.weekday;
  if (gpio.writeDateTime(rtcDateTime)) {
    std::snprintf(buffer, sizeof(buffer), "%02u:%02u saved to RTC", dt.hour, dt.minute);
  } else
#endif
  {
    std::snprintf(buffer, sizeof(buffer), "%02u:%02u synced", dt.hour, dt.minute);
  }
  message = buffer;
  state = State::DONE;
  wifiOff();
  render();
}

void TimeSyncActivity::wifiOff() {
  if (esp_sntp_enabled()) {
    esp_sntp_stop();
  }
  WiFi.disconnect(false);
  delay(100);
  WiFi.mode(WIFI_OFF);
  delay(100);
}
