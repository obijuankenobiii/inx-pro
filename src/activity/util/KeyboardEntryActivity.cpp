/**
 * @file KeyboardEntryActivity.cpp
 * @brief Definitions for KeyboardEntryActivity.
 */

#include "KeyboardEntryActivity.h"

#include <algorithm>
#include <cctype>
#include <cstring>

#include "images/Close.h"
#include "images/Delete.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "state/SystemSetting.h"

namespace {
constexpr int BOTTOM_MARGIN = 0;
constexpr int PAGE_MARGIN = 18;
constexpr int MAX_KEY_COLUMNS = 10;
constexpr int NUMPAD_MAX_KEY_SIZE = 107;
constexpr int NUMPAD_BOTTOM_MARGIN = 20;
/** Stack size (bytes) for xTaskCreate; 2048 overflowed with render() + GfxRenderer on ESP32-C3. */
constexpr uint32_t kDisplayTaskStackBytes = 8192;

struct NumpadKey {
  const char* number;
  const char* letters;
  const char* input;
};

constexpr NumpadKey NUMPAD_CAPS_KEYS[3][3] = {
    {{"ABC", "", "ABC"}, {"DEF", "", "DEF"}, {"GHI", "", "GHI"}},
    {{"JKL", "", "JKL"}, {"MNO", "", "MNO"}, {"PQRS", "", "PQRS"}},
    {{"TUV", "", "TUV"}, {"WXYZ", "", "WXYZ"}, {".,!?", "", ".,!?"}},
};

constexpr NumpadKey NUMPAD_NUM_KEYS[3][3] = {
    {{"1", "", "1"}, {"2", "", "2"}, {"3", "", "3"}},
    {{"4", "", "4"}, {"5", "", "5"}, {"6", "", "6"}},
    {{"7", "", "7"}, {"8", "", "8"}, {"9", "", "9"}},
};

constexpr NumpadKey NUMPAD_SYMBOL_KEYS[3][3] = {
    {{"!@#", "", "!@#"}, {"$%&", "", "$%&"}, {"*+-", "", "*+-"}},
    {{"/=:", "", "/=:"}, {"()[]", "", "()[]"}, {"{}<>\"", "", "{}<>\""}},
    {{"_'`", "", "_'`"}, {";:,.", "", ";:,."}, {"?\\|", "", "?\\|"}},
};

const NumpadKey& getNumpadKey(const int mode, const int row, const int col) {
  switch (mode) {
    case 2:
      return NUMPAD_NUM_KEYS[row][col];
    case 3:
      return NUMPAD_SYMBOL_KEYS[row][col];
    default:
      return NUMPAD_CAPS_KEYS[row][col];
  }
}

constexpr unsigned long NUMPAD_MULTI_TAP_TIMEOUT_MS = 900;
}  // namespace

const char* const KeyboardEntryActivity::keyboard[NUM_ROWS] = {
    "1234567890", "-=[]\\;',./", "qwertyuiop", "asdfghjkl ", "zxcvbnm   ", "^  ____<OK"};

const char* const KeyboardEntryActivity::keyboardShift[NUM_ROWS] = {
    "!@#$%^&*()", "_+{}|:\"<>?", "QWERTYUIOP", "ASDFGHJKL ", "ZXCVBNM   ", "SPECIAL ROW"};

void KeyboardEntryActivity::taskTrampoline(void* param) {
  auto* self = static_cast<KeyboardEntryActivity*>(param);
  self->displayTaskLoop();
}

void KeyboardEntryActivity::displayTaskLoop() {
  unsigned int renderCount = 0;
  while (true) {
    if (updateRequired) {
      updateRequired = false;
    } else {
      vTaskDelay(10 / portTICK_PERIOD_MS);
      continue;
    }

    xSemaphoreTake(renderingMutex, portMAX_DELAY);
    const unsigned long renderStartedAt = millis();
    ++renderCount;
    // Timing was gated to the WiFi prompt; make it unconditional so the draw cost and
    // the panel refresh cost can be told apart on any keyboard. `draw` is the framebuffer
    // work, `total` includes the blocking FAST_REFRESH inside render().
    const unsigned long drawStartedAt = millis();
    render();
    INX_SERIAL.printf("[%lu] [KEYBOARD] render=%u len=%u total=%lums\n", millis(), renderCount,
                      static_cast<unsigned>(text.length()), millis() - drawStartedAt);
    (void)renderStartedAt;
    xSemaphoreGive(renderingMutex);
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void KeyboardEntryActivity::onEnter() {
  Activity::onEnter();

  renderingMutex = xSemaphoreCreateMutex();
  updateRequired = true;

  const BaseType_t created = xTaskCreate(&KeyboardEntryActivity::taskTrampoline, "KeyboardEntryActivity",
                                         kDisplayTaskStackBytes, this, 1, &displayTaskHandle);
  if (title == "Enter WiFi Password") {
    INX_SERIAL.printf("[%lu] [KEYBOARD] wifi task result=%d handle=%p\n", millis(), static_cast<int>(created),
                   displayTaskHandle);
  }
}

void KeyboardEntryActivity::onExit() {
  Activity::onExit();

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }
  vSemaphoreDelete(renderingMutex);
  renderingMutex = nullptr;
}

bool KeyboardEntryActivity::isNumpadLayout() const {
  return SETTINGS.keyboardLayout == SystemSetting::KEYBOARD_NUMPAD;
}

int KeyboardEntryActivity::getRowCount() const { return isNumpadLayout() ? NUMPAD_ROWS : NUM_ROWS; }

bool KeyboardEntryActivity::isSpecialRow(const int row) const {
  return row == (isNumpadLayout() ? NUMPAD_ROWS - 1 : SPECIAL_ROW);
}

int KeyboardEntryActivity::getRowLength(const int row) const {
  if (row < 0 || row >= getRowCount()) return 0;
  if (isNumpadLayout()) return isSpecialRow(row) ? NUMPAD_ACTION_COLUMNS : NUMPAD_COLUMNS;
  return MAX_KEY_COLUMNS;
}

char KeyboardEntryActivity::getSelectedChar() const {
  const char* const* layout = (shiftActive || capsLockActive) ? keyboardShift : keyboard;

  if (isNumpadLayout() || selectedRow < 0 || selectedRow >= NUM_ROWS) return '\0';
  if (selectedCol < 0 || selectedCol >= getRowLength(selectedRow)) return '\0';

  return layout[selectedRow][selectedCol];
}

void KeyboardEntryActivity::handleNumpadKeyPress() {
  const int actionRow = NUMPAD_ROWS - 1;
  if (selectedRow == actionRow) {
    switch (selectedCol) {
      case 0:
        numpadMode = static_cast<NumpadMode>((static_cast<int>(numpadMode) + 1) % 4);
        multiTapRow = -1;
        return;
      case 1:
        if (maxLength == 0 || text.length() < maxLength) text += ' ';
        multiTapRow = -1;
        return;
      case 2:
        if (!text.empty()) text.pop_back();
        multiTapRow = -1;
        return;
      default:
        return;
    }
  }

  if (selectedRow < 0 || selectedRow >= NUMPAD_TEXT_ROWS || selectedCol < 0 || selectedCol >= NUMPAD_COLUMNS) {
    return;
  }

  const char* input = getNumpadKey(static_cast<int>(numpadMode), selectedRow, selectedCol).input;
  const size_t inputLength = strlen(input);
  if (inputLength == 0) return;

  const unsigned long now = millis();
  const bool continueMultiTap = multiTapRow == selectedRow && multiTapCol == selectedCol &&
                                now < multiTapDeadlineMs && !text.empty() && multiTapIndex + 1 < inputLength;
  const size_t nextIndex = continueMultiTap ? multiTapIndex + 1 : 0;

  char next = input[nextIndex];
  if (numpadMode == NumpadMode::LOWER && next >= 'A' && next <= 'Z') {
    next = static_cast<char>(std::tolower(static_cast<unsigned char>(next)));
  }
  if (continueMultiTap) {
    text.back() = next;
  } else if (maxLength == 0 || text.length() < maxLength) {
    text += next;
  } else {
    return;
  }

  multiTapRow = selectedRow;
  multiTapCol = selectedCol;
  multiTapIndex = nextIndex;
  multiTapDeadlineMs = now + NUMPAD_MULTI_TAP_TIMEOUT_MS;

}

void KeyboardEntryActivity::handleKeyPress() {
  if (isNumpadLayout()) {
    handleNumpadKeyPress();
    return;
  }

  if (selectedRow == SPECIAL_ROW) {
    if (selectedCol >= SHIFT_COL && selectedCol < SPACE_COL) {
      if (capsLockActive) {
        capsLockActive = false;
        shiftActive = false;
      } else if (shiftActive) {
        capsLockActive = true;
        shiftActive = false;
      } else {
        shiftActive = true;
      }
      return;
    }

    if (selectedCol >= SPACE_COL && selectedCol < BACKSPACE_COL) {
      if (maxLength == 0 || text.length() < maxLength) {
        text += ' ';
      }
      return;
    }

    if (selectedCol >= BACKSPACE_COL && selectedCol < DONE_COL) {
      if (!text.empty()) {
        text.pop_back();
      }
      return;
    }

    if (selectedCol >= DONE_COL) {
      if (onComplete) {
        onComplete(text);
      }
      return;
    }
  }

  const char c = getSelectedChar();
  if (c == '\0' || c == ' ') {
    return;
  }

  if (maxLength == 0 || text.length() < maxLength) {
    text += c;

    if (shiftActive && !capsLockActive && ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))) {
      shiftActive = false;
    }
  }
}

void KeyboardEntryActivity::loop() {
  // Touch taps are handled against the same full-width geometry used by render(). A tap both
  // selects and activates the key so the touchscreen does not require a second tap.
  if (mappedInput.hasTouch()) {
    if (mappedInput.wasTouchSwipeUp()) {
      if (onCancel) onCancel();
      return;
    }

    float tapNx = 0.0f;
    float tapNy = 0.0f;
    if (mappedInput.wasTouchTapInScreen(renderer, tapNx, tapNy)) {
      const int pageWidth = renderer.getScreenWidth();
      const int pageHeight = renderer.getScreenHeight();
      const int tapX = std::max(0, std::min(pageWidth - 1, static_cast<int>(tapNx * pageWidth)));
      const int tapY = static_cast<int>(tapNy * pageHeight);

      constexpr int closeSize = 40;
      if (tapX >= pageWidth - PAGE_MARGIN - closeSize && tapX < pageWidth - PAGE_MARGIN && tapY >= 20 &&
          tapY < 20 + closeSize) {
        if (onCancel) onCancel();
        return;
      }

      if (!isNumpadLayout()) {
        const std::string previous = text;
        const bool handled =
            SearchKeyboardLayout::Qwerty::tap(renderer, 140, pageHeight, tapX, tapY, qwerty_, text);
        if (maxLength > 0 && text.length() > maxLength) {
          text = previous;
        }
        if (SearchKeyboardLayout::Qwerty::consumeGo(qwerty_)) {
          if (onComplete) onComplete(text);
          return;
        }
        if (handled) {
          if (title == "Enter WiFi Password") {
            INX_SERIAL.printf("[%lu] [KEYBOARD] wifi input len=%u redraw\n", millis(),
                           static_cast<unsigned>(text.length()));
          }
          updateRequired = true;
        }
        // QWERTY owns the entire lower keyboard area. Do not let a tap in
        // its margins fall through to the retired six-row hit map.
        return;
      }

      const int rowCount = getRowCount();
      const int keySize = isNumpadLayout()
                              ? std::max(1, std::min({pageWidth / NUMPAD_COLUMNS, (pageHeight - 145) / rowCount,
                                                     NUMPAD_MAX_KEY_SIZE}))
                              : std::max(1, pageWidth / MAX_KEY_COLUMNS);
      const int keyboardWidth = isNumpadLayout() ? keySize * NUMPAD_COLUMNS : pageWidth;
      const int keyboardStartX = (pageWidth - keyboardWidth) / 2;
      const int keyboardBottomMargin = isNumpadLayout() ? NUMPAD_BOTTOM_MARGIN : BOTTOM_MARGIN;
      const int keyboardStartY = pageHeight - rowCount * keySize - keyboardBottomMargin;
      if (tapY >= keyboardStartY && tapY < keyboardStartY + rowCount * keySize) {
        const int row = (tapY - keyboardStartY) / keySize;
        selectedRow = row;
        if (isNumpadLayout() && (tapX < keyboardStartX || tapX >= keyboardStartX + keyboardWidth)) {
          return;
        }
        if (isNumpadLayout() && row == rowCount - 1) {
          const int relativeX = tapX - keyboardStartX;
          selectedCol = std::min(NUMPAD_ACTION_COLUMNS - 1, (relativeX * NUMPAD_ACTION_COLUMNS) / keyboardWidth);
        } else if (isNumpadLayout()) {
          const int relativeX = tapX - keyboardStartX;
          selectedCol = std::min(NUMPAD_COLUMNS - 1, (relativeX * NUMPAD_COLUMNS) / keyboardWidth);
        } else if (row == SPECIAL_ROW) {
          constexpr int SPECIAL_UNITS = MAX_KEY_COLUMNS;
          const int unit = std::min(SPECIAL_UNITS - 1, (tapX * SPECIAL_UNITS) / pageWidth);
          if (unit < SPACE_COL) {
            selectedCol = SHIFT_COL;
          } else if (unit < BACKSPACE_COL) {
            selectedCol = SPACE_COL;
          } else if (unit < DONE_COL) {
            selectedCol = BACKSPACE_COL;
          } else {
            selectedCol = DONE_COL;
          }
        } else {
          const int rowLength = getRowLength(row);
          selectedCol = std::min(rowLength - 1, (tapX * rowLength) / pageWidth);
        }
        handleKeyPress();
        updateRequired = true;
        return;
      }
    }
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    if (selectedRow > 0) {
      selectedRow--;

      const int maxCol = getRowLength(selectedRow) - 1;
      if (selectedCol > maxCol) selectedCol = maxCol;
    } else {
      selectedRow = getRowCount() - 1;
      const int maxCol = getRowLength(selectedRow) - 1;
      if (selectedCol > maxCol) selectedCol = maxCol;
    }
    updateRequired = true;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    if (selectedRow < getRowCount() - 1) {
      selectedRow++;
      const int maxCol = getRowLength(selectedRow) - 1;
      if (selectedCol > maxCol) selectedCol = maxCol;
    } else {
      selectedRow = 0;
      const int maxCol = getRowLength(selectedRow) - 1;
      if (selectedCol > maxCol) selectedCol = maxCol;
    }
    updateRequired = true;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    const int maxCol = getRowLength(selectedRow) - 1;

    if (isSpecialRow(selectedRow)) {
      if (isNumpadLayout()) {
        selectedCol = selectedCol > 0 ? selectedCol - 1 : NUMPAD_ACTION_COLUMNS - 1;
        updateRequired = true;
        return;
      }
      if (selectedCol >= SHIFT_COL && selectedCol < SPACE_COL) {
        selectedCol = maxCol;
      } else if (selectedCol >= SPACE_COL && selectedCol < BACKSPACE_COL) {
        selectedCol = SHIFT_COL;
      } else if (selectedCol >= BACKSPACE_COL && selectedCol < DONE_COL) {
        selectedCol = SPACE_COL;
      } else if (selectedCol >= DONE_COL) {
        selectedCol = BACKSPACE_COL;
      }
      updateRequired = true;
      return;
    }

    if (selectedCol > 0) {
      selectedCol--;
    } else {
      selectedCol = maxCol;
    }
    updateRequired = true;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    const int maxCol = getRowLength(selectedRow) - 1;

    if (isSpecialRow(selectedRow)) {
      if (isNumpadLayout()) {
        selectedCol = selectedCol < NUMPAD_ACTION_COLUMNS - 1 ? selectedCol + 1 : 0;
        updateRequired = true;
        return;
      }
      if (selectedCol >= SHIFT_COL && selectedCol < SPACE_COL) {
        selectedCol = SPACE_COL;
      } else if (selectedCol >= SPACE_COL && selectedCol < BACKSPACE_COL) {
        selectedCol = BACKSPACE_COL;
      } else if (selectedCol >= BACKSPACE_COL && selectedCol < DONE_COL) {
        selectedCol = DONE_COL;
      } else if (selectedCol >= DONE_COL) {
        selectedCol = SHIFT_COL;
      }
      updateRequired = true;
      return;
    }

    if (selectedCol < maxCol) {
      selectedCol++;
    } else {
      selectedCol = 0;
    }
    updateRequired = true;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    handleKeyPress();
    updateRequired = true;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (onCancel) {
      onCancel();
    }
    updateRequired = true;
  }
}

void KeyboardEntryActivity::render() const {
  renderEnteredAt_ = millis();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  constexpr int titleFont = MONTSERRAT_16_FONT_ID;
  const int inputFont = systemFontId();
  const int keyFont = systemFontId();
  const int hintFont = systemFontId();

  renderer.text.render(titleFont, PAGE_MARGIN, 22, title.c_str(), true, EpdFontFamily::BOLD);
  constexpr int closeSize = 40;
  renderer.bitmap.icon(Close, pageWidth - PAGE_MARGIN - closeSize, 20, closeSize, closeSize,
                       BitmapRender::Orientation::None, false);

  std::string displayText;
  if (isPassword) {
    displayText = std::string(text.length(), '*');
  } else {
    displayText = text;
  }

  displayText += "_";

  const int inputX = PAGE_MARGIN;
  const int inputY = 82;
  const int inputW = pageWidth - PAGE_MARGIN * 2;
  constexpr int inputH = 56;
  renderer.rectangle.render(inputX, inputY, inputW, inputH, true, true);

  std::string inputLine = renderer.text.truncate(inputFont, displayText.c_str(), inputW - 24);
  const int inputTextY = inputY + (inputH - renderer.text.getLineHeight(inputFont)) / 2;
  renderer.text.render(inputFont, inputX + 12, inputTextY, inputLine.c_str(), true);

  if (maxLength > 0) {
    char countText[24];
    snprintf(countText, sizeof(countText), "%u/%u", static_cast<unsigned>(text.length()),
             static_cast<unsigned>(maxLength));
    const int countW = renderer.text.getWidth(hintFont, countText);
    renderer.text.render(hintFont, inputX + inputW - countW - 10, inputY + inputH + 8, countText, true);
  }

  if (!isNumpadLayout()) {
    SearchKeyboardLayout::Qwerty::render(renderer, inputY + inputH + 22, pageHeight, qwerty_);
    const unsigned long drawDoneAt = millis();
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    INX_SERIAL.printf("[%lu] [KEYBOARD] qwerty draw=%lums refresh=%lums\n", millis(),
                      drawDoneAt - renderEnteredAt_, millis() - drawDoneAt);
    return;
  }

  const int rowCount = getRowCount();
  const int keySize = isNumpadLayout()
                          ? std::max(1, std::min({pageWidth / NUMPAD_COLUMNS, (pageHeight - 145) / rowCount,
                                                 NUMPAD_MAX_KEY_SIZE}))
                          : std::max(1, pageWidth / MAX_KEY_COLUMNS);
  const int keyboardWidth = isNumpadLayout() ? keySize * NUMPAD_COLUMNS : pageWidth;
  const int keyboardStartX = (pageWidth - keyboardWidth) / 2;
  const int keyboardAreaHeight = rowCount * keySize;
  const int keyboardBottomMargin = isNumpadLayout() ? NUMPAD_BOTTOM_MARGIN : BOTTOM_MARGIN;
  const int keyboardStartY = pageHeight - keyboardAreaHeight - keyboardBottomMargin;

  const char* const* layout = (shiftActive || capsLockActive) ? keyboardShift : keyboard;

  auto drawKey = [&](const int x, const int y, const int w, const int h, const char* label, const bool selected,
                     const bool emphasized = false) {
    const int labelW =
        renderer.text.getWidth(keyFont, label, emphasized ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    const int labelX = x + (w - labelW) / 2;
    const int labelY = y + (h - renderer.text.getLineHeight(keyFont)) / 2;
    if (selected) {
      renderer.rectangle.fill(x, y, w, h, true, false);
      renderer.line.render(x, y, x + w - 1, y, true);
      renderer.line.render(x + w - 1, y, x + w - 1, y + h - 1, true);
      renderer.text.render(keyFont, labelX, labelY, label, false,
                           emphasized ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
      return;
    }

    renderer.rectangle.fill(x, y, w, h, false, false);
    renderer.line.render(x, y, x + w - 1, y, true);
    renderer.line.render(x + w - 1, y, x + w - 1, y + h - 1, true);
    renderer.text.render(keyFont, labelX, labelY, label, true,
                         emphasized ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
  };

  auto drawNumpadKey = [&](const int x, const int y, const int w, const int h, const NumpadKey& key,
                           const bool selected) {
    const bool ink = !selected;
    if (selected) {
      renderer.rectangle.fill(x, y, w, h, true, false);
    } else {
      renderer.rectangle.fill(x, y, w, h, false, false);
    }
    renderer.line.render(x, y, x + w - 1, y, true);
    renderer.line.render(x + w - 1, y, x + w - 1, y + h - 1, true);

    const int numpadFont = systemFontId();
    char label[8];
    strlcpy(label, key.number, sizeof(label));
    if (numpadMode == NumpadMode::LOWER) {
      for (size_t i = 0; label[i] != '\0'; ++i) {
        label[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(label[i])));
      }
    }
    const int labelW = renderer.text.getWidth(numpadFont, label);
    const int labelY = y + (h - renderer.text.getLineHeight(numpadFont)) / 2;
    renderer.text.render(numpadFont, x + (w - labelW) / 2, labelY, label, ink);

    if (key.letters[0] != '\0') {
      char letters[sizeof("WXYZ")];
      size_t i = 0;
      for (; key.letters[i] != '\0' && i + 1 < sizeof(letters); ++i) {
        const char c = key.letters[i];
        letters[i] = (shiftActive || capsLockActive)
                         ? c
                         : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      }
      letters[i] = '\0';
      const int lettersW = renderer.text.getWidth(keyFont, letters);
      const int lettersY = y + h / 2 + 2;
      renderer.text.render(keyFont, x + (w - lettersW) / 2, lettersY, letters, ink);
    }
  };

  for (int row = 0; row < rowCount; row++) {
    const int rowY = keyboardStartY + row * keySize;
    const int rowLength = getRowLength(row);

    if (isNumpadLayout()) {
      if (isSpecialRow(row)) {
        const int actionWidth = keyboardWidth / NUMPAD_ACTION_COLUMNS;
        const char* modeLabel = numpadMode == NumpadMode::LOWER ? "abc" :
                                (numpadMode == NumpadMode::CAPS ? "CAPS" :
                                 (numpadMode == NumpadMode::NUM ? "NUM" : "SYMBOL"));
        const char* actionLabels[2] = {modeLabel, "SPACE"};
        for (int col = 0; col < NUMPAD_ACTION_COLUMNS; ++col) {
          const int x = keyboardStartX + col * actionWidth;
          const int w = col == NUMPAD_ACTION_COLUMNS - 1 ? keyboardStartX + keyboardWidth - x : actionWidth;
          const bool selected = selectedRow == row && selectedCol == col;
          if (col < 2) {
            drawKey(x, rowY, w, keySize, actionLabels[col], selected, col == 0);
          } else {
            drawKey(x, rowY, w, keySize, "", selected);
            constexpr int DELETE_ICON_SIZE = 38;
            renderer.bitmap.iconScaled(Delete, x + (w - DELETE_ICON_SIZE) / 2, rowY + (keySize - DELETE_ICON_SIZE) / 2,
                                        30, 30, DELETE_ICON_SIZE, DELETE_ICON_SIZE,
                                        BitmapRender::Orientation::None, selected);
          }
        }
      } else {
        for (int col = 0; col < NUMPAD_COLUMNS; ++col) {
          const int keyX = keyboardStartX + col * keySize;
          const int keyW = col == NUMPAD_COLUMNS - 1 ? keyboardWidth - col * keySize : keySize;
          drawNumpadKey(keyX, rowY, keyW, keySize,
                        getNumpadKey(static_cast<int>(numpadMode), row, col), selectedRow == row && selectedCol == col);
        }
      }
    } else if (row == SPECIAL_ROW) {
      constexpr int SPECIAL_UNITS = MAX_KEY_COLUMNS;
      const int shiftWidth = (pageWidth * 2) / SPECIAL_UNITS;
      const int spaceWidth = (pageWidth * 4) / SPECIAL_UNITS;
      const int backspaceWidth = (pageWidth * 2) / SPECIAL_UNITS;
      const int okWidth = pageWidth - shiftWidth - spaceWidth - backspaceWidth;

      int currentX = 0;

      const bool shiftSelected = (selectedRow == SPECIAL_ROW && selectedCol >= SHIFT_COL && selectedCol < SPACE_COL);
      const char* shiftLabel = capsLockActive ? "CAPS" : (shiftActive ? "SHIFT" : "Aa");
      drawKey(currentX, rowY, shiftWidth, keySize, shiftLabel, shiftSelected, shiftActive || capsLockActive);
      currentX += shiftWidth;

      const bool spaceSelected = (selectedRow == SPECIAL_ROW && selectedCol >= SPACE_COL && selectedCol < BACKSPACE_COL);
      drawKey(currentX, rowY, spaceWidth, keySize, "SPACE", spaceSelected);
      currentX += spaceWidth;

      const bool bsSelected = (selectedRow == SPECIAL_ROW && selectedCol >= BACKSPACE_COL && selectedCol < DONE_COL);
      drawKey(currentX, rowY, backspaceWidth, keySize, "DEL", bsSelected);
      currentX += backspaceWidth;

      const bool okSelected = (selectedRow == SPECIAL_ROW && selectedCol >= DONE_COL);
      drawKey(currentX, rowY, okWidth, keySize, "OK", okSelected, true);
    } else {
      for (int col = 0; col < rowLength; col++) {
        const char c = layout[row][col];
        char keyLabel[2] = {c, '\0'};

        const int keyX = (pageWidth * col) / rowLength;
        const int keyRight = (pageWidth * (col + 1)) / rowLength;
        const bool isSelected = row == selectedRow && col == selectedCol;
        drawKey(keyX, rowY, keyRight - keyX, keySize, keyLabel, isSelected);
      }
    }
  }

  if (isNumpadLayout()) {
    const int keyboardBottomY = keyboardStartY + keyboardAreaHeight - 1;
    renderer.line.render(keyboardStartX, keyboardStartY, keyboardStartX, keyboardBottomY, true);
    renderer.line.render(keyboardStartX, keyboardBottomY, keyboardStartX + keyboardWidth - 1, keyboardBottomY, true);
  }

  const auto labels = mappedInput.mapLabels("Back", "Select", "Prev", "Next");

  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
