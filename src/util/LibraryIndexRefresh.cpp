#include "LibraryIndexRefresh.h"

#include <Arduino.h>
#include <HalDisplay.h>
#include <SDCardManager.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include <GfxRenderer.h>

#include "../activity/Activity.h"
#include "../state/SystemSetting.h"
#include "../system/ScreenComponents.h"
#include "LibraryIndex.h"

namespace {

volatile bool running = false;
bool popup = false;
Activity* screen = nullptr;
uint8_t* frame = nullptr;
size_t frameSize = 0;
ScreenComponents::LoadingProgressLayout layout{};
volatile int current = 0;
volatile int total = 0;
int displayedProgress = -1;
constexpr uint32_t stackSize = 16384;

void clearFrame() {
  free(frame);
  frame = nullptr;
  frameSize = 0;
  screen = nullptr;
  popup = false;
  layout = {};
  current = 0;
  total = 0;
  displayedProgress = -1;
}

}

void LibraryIndexRefresh::start(GfxRenderer& renderer, Activity* currentScreen) {
  if (running || popup) return;

  renderer.syncWriteBufferFromActive();
  frameSize = renderer.getBufferSize();
  frame = static_cast<uint8_t*>(malloc(frameSize));
  if (frame && renderer.getFrameBuffer()) {
    memcpy(frame, renderer.getFrameBuffer(), frameSize);
  }

  layout = ScreenComponents::LoadingProgress::show(renderer, "Updating", 0);
  displayedProgress = 0;
  running = true;
  popup = true;
  screen = currentScreen;

  const BaseType_t created = xTaskCreate(
      [](void*) {
        LibraryIndex::indexAll([](int indexed, int count, const char*) {
          current = indexed;
          total = count;
          if (indexed % 10 == 0) vTaskDelay(pdMS_TO_TICKS(1));
        });
        SETTINGS.useLibraryIndex = 1;
        SETTINGS.saveToFile();
        running = false;
        vTaskDelete(nullptr);
      },
      "GlobalLibraryIndex", stackSize, nullptr, 1, nullptr);

  if (created != pdPASS) running = false;
}

void LibraryIndexRefresh::render(GfxRenderer& renderer) {
  if (!popup || !running || total <= 0) return;

  const int progress = std::max(0, std::min(100, current * 100 / total));
  if (progress == displayedProgress) return;

  ScreenComponents::LoadingProgress::setProgress(renderer, layout, progress);
  displayedProgress = progress;
}

void LibraryIndexRefresh::finish(GfxRenderer& renderer, Activity* currentScreen) {
  if (!popup || running) return;

  if (currentScreen == screen) {
    if (frame && renderer.getFrameBuffer() && frameSize == renderer.getBufferSize()) {
      memcpy(renderer.getFrameBuffer(), frame, frameSize);
    }
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  }
  clearFrame();
}

bool LibraryIndexRefresh::isRunning() { return running; }
