#pragma once

#include <functional>
#include <string>
#include <vector>

#include "activity/ActivityWithSubactivity.h"
#include "activity/page/components/search/SearchKeyboard.h"

class TimeSyncActivity final : public ActivityWithSubactivity {
 public:
  TimeSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::function<void()>& onBack)
      : ActivityWithSubactivity("TimeSync", renderer, mappedInput), onBack(onBack) {}

  void onEnter() override;
  void loop() override;

 private:
  enum class State { CONNECTING, LOADING_TIMEZONES, SELECTING_TIMEZONE, SYNCING, DONE, FAILED };

  State state = State::CONNECTING;
  std::string message = "Connect to WiFi";
  std::function<void()> onBack;
  std::vector<std::string> timezoneOptions;
  std::vector<std::string> filteredTimezoneOptions;
  int timezoneScrollOffset = 0;
  int timezoneSelection = 0;
  SearchKeyboard timezoneKeyboard;
  std::string timezoneQuery;
  bool timezoneKeyboardCollapsed = true;

  void render();
  void beginWifiOrSync();
  void onWifiComplete(bool connected);
  void startTimezoneSelection();
  bool loadTimezoneOptions();
  bool loadTimezoneOffset(const std::string& timezone, uint8_t& quarterOffset) const;
  void applyTimezoneFilter();
  void selectTimezone(int index);
  void moveTimezoneSelection(int delta);
  void performSync();
  void wifiOff();
};
