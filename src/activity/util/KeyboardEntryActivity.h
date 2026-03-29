#pragma once

/**
 * @file KeyboardEntryActivity.h
 * @brief Public interface and types for KeyboardEntryActivity.
 */

#include <GfxRenderer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <functional>
#include <string>
#include <utility>

#include "activity/page/components/search/SearchQwerty.h"
#include "../Activity.h"

/**
 * Reusable keyboard entry activity for text input.
 * Can be started from any activity that needs text entry.
 *
 * Usage:
 *   1. Create a KeyboardEntryActivity instance
 *   2. Set callbacks with setOnComplete() and setOnCancel()
 *   3. Call onEnter() to start the activity
 *   4. Call loop() in your main loop
 *   5. When complete or cancelled, callbacks will be invoked
 */
class KeyboardEntryActivity : public Activity {
 public:
  using OnCompleteCallback = std::function<void(const std::string&)>;
  using OnCancelCallback = std::function<void()>;

  /**
   * Constructor
   * @param renderer Reference to the GfxRenderer for drawing
   * @param mappedInput Reference to MappedInputManager for handling input
   * @param title Title to display above the keyboard
   * @param initialText Initial text to show in the input field
   * @param startY Y position to start rendering the keyboard
   * @param maxLength Maximum length of input text (0 for unlimited)
   * @param isPassword If true, display asterisks instead of actual characters
   * @param onComplete Callback invoked when input is complete
   * @param onCancel Callback invoked when input is cancelled
   */
  explicit KeyboardEntryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                 std::string title = "Enter Text", std::string initialText = "", const int startY = 10,
                                 const size_t maxLength = 0, const bool isPassword = false,
                                 OnCompleteCallback onComplete = nullptr, OnCancelCallback onCancel = nullptr)
      : Activity("KeyboardEntry", renderer, mappedInput),
        title(std::move(title)),
        text(std::move(initialText)),
        startY(startY),
        maxLength(maxLength),
        isPassword(isPassword),
        onComplete(std::move(onComplete)),
        onCancel(std::move(onCancel)) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;

 private:
  std::string title;
  int startY;
  std::string text;
  size_t maxLength;
  bool isPassword;
  TaskHandle_t displayTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  bool updateRequired = false;
  mutable unsigned long renderEnteredAt_ = 0;  ///< draw-vs-refresh timing split

  int selectedRow = 0;
  int selectedCol = 0;
  bool shiftActive = false;
  bool capsLockActive = false;
  SearchKeyboardLayout::Qwerty::State qwerty_;
  enum class NumpadMode { LOWER = 0, CAPS = 1, NUM = 2, SYMBOL = 3 };
  NumpadMode numpadMode = NumpadMode::LOWER;
  int multiTapRow = -1;
  int multiTapCol = -1;
  size_t multiTapIndex = 0;
  unsigned long multiTapDeadlineMs = 0;

  OnCompleteCallback onComplete;
  OnCancelCallback onCancel;

  static constexpr int NUM_ROWS = 6;
  static constexpr int NUMPAD_ROWS = 4;
  static constexpr int NUMPAD_TEXT_ROWS = 3;
  static constexpr int NUMPAD_COLUMNS = 3;
  static constexpr int NUMPAD_ACTION_COLUMNS = 3;
  static const char* const keyboard[NUM_ROWS];
  static const char* const keyboardShift[NUM_ROWS];

  static constexpr int SPECIAL_ROW = 5;
  static constexpr int SHIFT_COL = 0;
  static constexpr int SPACE_COL = 2;
  static constexpr int BACKSPACE_COL = 6;
  static constexpr int DONE_COL = 8;

  static void taskTrampoline(void* param);
  [[noreturn]] void displayTaskLoop();
  bool isNumpadLayout() const;
  int getRowCount() const;
  bool isSpecialRow(int row) const;
  char getSelectedChar() const;
  void handleKeyPress();
  void handleNumpadKeyPress();
  int getRowLength(int row) const;
  void render() const;
  void renderItemWithSelector(int x, int y, const char* item, bool isSelected) const;
};
