#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <functional>

#include "activity/Activity.h"

/** Selectively builds the library's pre-grouped metadata indexes and thumbnails. */
class MetadataGeneratorActivity final : public Activity {
 public:
  explicit MetadataGeneratorActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                     const std::function<void()>& goBack)
      : Activity("MetadataGenerator", renderer, mappedInput), goBack(goBack) {}

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
  void toggleOption(int index);
  bool selectedAnything() const;

  const std::function<void()> goBack;
  TaskHandle_t workerTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  volatile bool updateRequired = false;
  volatile bool cancelRequested = false;
  volatile State state = READY;
  volatile bool thumbnailPhase = false;
  volatile int processedCount = 0;
  volatile int totalCount = 0;
  char currentPath[180] = {0};
  char completionSummary[120] = {0};
  int optionListTop = 0;
  int optionListRows = 0;
  int scrollOffset = 0;
  bool selectedOptions[12] = {false, true, true, true, true, true, true, true, true, true, true, true};
};
