#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "activity/ActivityWithSubactivity.h"
#include "system/PluginManager.h"

/** Downloads and enables optional capability-based reader plugins. */
class PluginManagerActivity final : public ActivityWithSubactivity {
 public:
  PluginManagerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::function<void()> goBack)
      : ActivityWithSubactivity("PluginManager", renderer, mappedInput), goBack_(std::move(goBack)) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool preventAutoSleep() override { return state_ == State::Downloading; }

 private:
  enum class State : uint8_t { Ready, Downloading, Failed };

  const std::function<void()> goBack_;
  std::vector<PluginManager::Package> packages_;
  std::string status_;
  volatile State state_ = State::Ready;
  volatile size_t downloaded_ = 0;
  volatile size_t total_ = 0;
  volatile bool updateRequired_ = false;
  volatile bool shuttingDown_ = false;
  bool showCompletionCheck_ = false;
  volatile uint32_t completionCheckExpiresAt_ = 0;
  TaskHandle_t workerTask_ = nullptr;
  TaskHandle_t displayTask_ = nullptr;
  SemaphoreHandle_t renderingMutex_ = nullptr;
  volatile int lastPercent_ = -1;

  void loadPackages();
  void installSelected();
  void launchWifiSelection();
  void onWifiSelectionComplete(bool connected);
  void startInstallation();
  static void workerTaskTrampoline(void* param);
  static void displayTaskTrampoline(void* param);
  [[noreturn]] void workerTaskLoop();
  [[noreturn]] void displayTaskLoop();
  void render();
};
