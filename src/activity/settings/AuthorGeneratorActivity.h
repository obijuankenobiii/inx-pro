#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <functional>

#include "activity/ActivityWithSubactivity.h"

/** Builds the separate author catalog used by the Library Author view. */
class AuthorGeneratorActivity final : public ActivityWithSubactivity {
 public:
  explicit AuthorGeneratorActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                   const std::function<void()>& goBack)
      : ActivityWithSubactivity("AuthorGenerator", renderer, mappedInput), goBack(goBack) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool preventAutoSleep() override { return state == RUNNING; }

 private:
  enum State : uint8_t { READY, RUNNING, SUCCESS, CANCELLED, FAILED };

  static void workerTaskTrampoline(void* param);
  void workerTaskLoop();
  void render();
  void startGeneration();

  const std::function<void()> goBack;
  TaskHandle_t workerTaskHandle = nullptr;
  volatile bool updateRequired = false;
  volatile bool cancelRequested = false;
  volatile State state = READY;
  volatile int processedCount = 0;
  volatile int totalCount = 0;
  char currentPath[256] = {0};
};
