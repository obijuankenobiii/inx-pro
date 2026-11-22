#pragma once

/**
 * @file SleepImagePickerActivity.h
 * @brief Public interface and types for SleepImagePickerActivity.
 */

#include <HalDisplay.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "activity/ActivityWithSubactivity.h"

/**
 * Lists image files from /sleep or /widgets so the user can select an image for sleep or a home widget.
 */
class SleepImagePickerActivity final : public ActivityWithSubactivity {
 public:
  using SelectionCallback = std::function<void(const std::string&)>;

  explicit SleepImagePickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                    const std::function<void()>& onBack, SelectionCallback onSelected = {},
                                    std::string initialPath = {}, std::string title = "Choose sleep image",
                                    bool allowRandom = true)
      : ActivityWithSubactivity("SleepImagePicker", renderer, mappedInput), onBack(onBack),
        onSelected(std::move(onSelected)), initialPath(std::move(initialPath)), title(std::move(title)),
        allowRandom(allowRandom) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;

 private:
  struct Row {
    std::string label;
    std::string value;
    std::string previewPath;
  };

  bool updateRequired = false;
  TaskHandle_t displayTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;

  std::vector<Row> rows;
  int selectedIndex = 0;
  bool randomEnabled = false;
  SelectionCallback onSelected;
  std::string initialPath;
  std::string title;
  bool allowRandom = true;
  int renderedPageStart = -1;
  uint8_t* gridBuffer = nullptr;
  bool gridBufferStored = false;
  int gridBufferPageStart = -1;

  const std::function<void()> onBack;

  static void taskTrampoline(void* param);
  [[noreturn]] void displayTaskLoop();

  void rebuildRows();
  void render();
  void drawPickerChrome(int pageStart, int rowCount, bool hasImages, bool localRandomEnabled, bool drawCells = true);
  void drawPickerThumbnails(int pageStart, int rowCount);
  void drawSelectionFrame(int pageStart, int rowCount, int index);
  int pageStartForIndex(int index) const;
  int slotForIndex(int pageStart, int index) const;
  int indexForSlot(int pageStart, int slot) const;
  bool storeGridBuffer(int pageStart);
  bool restoreGridBuffer(int pageStart);
  void freeGridBuffer();
  void applySelection();
  bool paginate(int direction);
  void requestRedraw();
};
