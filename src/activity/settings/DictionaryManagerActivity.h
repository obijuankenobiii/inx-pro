#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "activity/ActivityWithSubactivity.h"
#include "activity/page/Page.h"
#include "system/DictionaryPackageManager.h"

/** Downloads and removes compressed StarDict dictionaries. */
class DictionaryManagerActivity final : public ActivityWithSubactivity {
 public:
  DictionaryManagerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                             std::function<void()> goBack)
      : ActivityWithSubactivity("DictionaryManager", renderer, mappedInput), goBack_(std::move(goBack)) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool preventAutoSleep() override { return state_ == State::Downloading; }

 private:
  enum class State : uint8_t { Ready, Downloading, Failed };

  static constexpr int kRowHeight = Page::LIST_ITEM_HEIGHT;

  const std::function<void()> goBack_;
  std::vector<DictionaryPackageManager::Package> packages_;
  std::string status_;
  int selectedIndex_ = 0;
  int installingPackageIndex_ = -1;
  volatile size_t progressDownloaded_ = 0;
  volatile size_t progressTotal_ = 0;
  volatile bool updateRequired_ = false;
  volatile State state_ = State::Ready;
  TaskHandle_t displayTaskHandle_ = nullptr;
  TaskHandle_t installTaskHandle_ = nullptr;
  SemaphoreHandle_t renderingMutex_ = nullptr;
  volatile bool shuttingDown_ = false;
  volatile int lastProgressPercent_ = -1;
  volatile unsigned long lastProgressUpdateMs_ = 0;

  void loadPackages();
  void installSelected();
  void startInstallation();
  void launchWifiSelection();
  void onWifiSelectionComplete(bool connected);
  static void displayTaskTrampoline(void* param);
  static void installTaskTrampoline(void* param);
  [[noreturn]] void displayTaskLoop();
  void installTaskLoop();
  void render();
  void updateDisplay();
};
