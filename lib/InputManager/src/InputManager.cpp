#include "InputManager.h"

#include <algorithm>

#include "MultiTouchGestureMath.h"

#if FREEINK_CAP_TOUCH
#include <Wire.h>
#include <I2cBusLock.h>
#include <driver/gpio.h>
#if FREEINK_TOUCH_TMA525C
#include <Logging.h>
#endif
#if FREEINK_DEVICE_PICCO
#include <driver/rtc_io.h>
#endif
#if FREEINK_DEVICE_MURPHY_M4
#include <driver/i2c_master.h>
#include <esp_rom_sys.h>
#endif
#if FREEINK_DEVICE_EEGO_A4
#include <Logging.h>

#include "gsl/EegoA4GslFirmware.h"
#endif
#if FREEINK_TOUCH_GT911 && FREEINK_GT911_INT_WAKE
#include <Logging.h>
#endif
#endif
#if FREEINK_DEVICE_PAPERMONO
#include <PaperMonoBoard.h>
#endif
#if defined(TOUCH_PROBE_DEBUG)
#include <esp_rom_sys.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#endif















const int InputManager::ADC_RANGES_1[] = {ADC_NO_BUTTON, 3100, 2090, 750, INT32_MIN};
const int InputManager::ADC_RANGES_2[] = {ADC_NO_BUTTON, 1120, INT32_MIN};
const char* InputManager::BUTTON_NAMES[] = {"Back", "Confirm", "Left", "Right", "Up", "Down", "Power"};

namespace {
int absInt(const int value) { return value < 0 ? -value : value; }

#if defined(TOUCH_PROBE_DEBUG)
void touchDebugPrintf(const char* format, ...) {
  char buf[192];
  va_list args;
  va_start(args, format);
  const int len = vsnprintf(buf, sizeof(buf), format, args);
  va_end(args);
  if (len < 0) return;
  const size_t n = strnlen(buf, sizeof(buf));
#if FREEINK_LOG_TRANSPORT == FREEINK_LOG_TRANSPORT_USB_CDC_WRITE
  Serial.write(reinterpret_cast<const uint8_t*>(buf), n);
#elif FREEINK_LOG_TRANSPORT == FREEINK_LOG_TRANSPORT_ROM_PRINTF
  esp_rom_printf("%s", buf);
#else
  if (Serial) {
    Serial.print(buf);
  }
#endif
}
#endif
}

InputManager::InputManager()
    : currentState(0),
      lastState(0),
      pressedEvents(0),
      releasedEvents(0),
      lastDebounceTime(0),
      buttonPressStart(0),
      buttonPressFinish(0),
      powerButtonPressStart(0),
      powerButtonPressFinish(0),
      confirmBackPressStart(0),
      confirmBackPhysicalPressed(false),
      confirmBackLongPressActive(false),
      confirmPowerPressStart(0),
      confirmPowerPhysicalPressed(false),
      confirmPowerLongPressActive(false),
      twoButtonPhysicalState(0),
      twoButtonPressStart(0),
      twoButtonLongPressActive(false) {}

#if FREEINK_DEVICE_METALIO_EINK4

static constexpr uint32_t METALIO_BOOT_WAIT_MS = 3000;
#endif

void InputManager::begin() {
#if FREEINK_DEVICE_PICCO

  if (BoardConfig::isPicco() && BoardConfig::ACTIVE.input.power >= 0) {
    detachInterrupt(BoardConfig::ACTIVE.input.power);
  }
#endif
#if FREEINK_DEVICE_METALIO_EINK4
  if (BoardConfig::isMetalioEInk4()) freeink::metalio::ensureBooted(METALIO_BOOT_WAIT_MS);
#endif
  if (hasToggleSwitch()) {
    pinMode(BoardConfig::ACTIVE.input.toggleSwitch, INPUT_PULLUP);
    toggleSwitchOn = toggleSwitchRaw = digitalRead(BoardConfig::ACTIVE.input.toggleSwitch) == LOW;
  }
  if (BoardConfig::ACTIVE.inputStyle == BoardConfig::InputStyle::XteinkAdcLadder) {
    pinMode(BUTTON_ADC_PIN_1, INPUT);
    pinMode(BUTTON_ADC_PIN_2, INPUT);
    pinMode(BoardConfig::ACTIVE.input.power, BoardConfig::ACTIVE.input.powerActiveHigh ? INPUT_PULLDOWN : INPUT_PULLUP);
    analogSetAttenuation(ADC_11db);
    beginTouch();
    return;
  }

  if (BoardConfig::ACTIVE.inputStyle == BoardConfig::InputStyle::OnePageAdcLadder) {
    if (BoardConfig::ACTIVE.input.adcLadderPin >= 0) {
      pinMode(BoardConfig::ACTIVE.input.adcLadderPin, INPUT);
    }
    analogSetAttenuation(ADC_11db);
    if (BoardConfig::ACTIVE.input.up >= 0) pinMode(BoardConfig::ACTIVE.input.up, INPUT_PULLUP);
    if (BoardConfig::ACTIVE.input.down >= 0) pinMode(BoardConfig::ACTIVE.input.down, INPUT_PULLUP);
    if (BoardConfig::ACTIVE.input.power >= 0) {
      pinMode(BoardConfig::ACTIVE.input.power,
              BoardConfig::ACTIVE.input.powerActiveHigh ? INPUT_PULLDOWN : INPUT_PULLUP);
    }
    beginTouch();
    return;
  }

  const int8_t pins[] = {BoardConfig::ACTIVE.input.back, BoardConfig::ACTIVE.input.confirm,
                         BoardConfig::ACTIVE.input.left, BoardConfig::ACTIVE.input.right,
                         BoardConfig::ACTIVE.input.up,   BoardConfig::ACTIVE.input.down};
  for (const int8_t pin : pins) {
    if (pin >= 0) {
      pinMode(pin, INPUT_PULLUP);
    }
  }





  if (BoardConfig::ACTIVE.input.power >= 0) {
    pinMode(BoardConfig::ACTIVE.input.power, BoardConfig::ACTIVE.input.powerActiveHigh ? INPUT_PULLDOWN : INPUT_PULLUP);
  }
  beginTouch();
}

int InputManager::getButtonFromADC(const int adcValue, const int ranges[], const int numButtons) {
  for (int i = 0; i < numButtons; i++) {
    if (ranges[i + 1] < adcValue && adcValue <= ranges[i]) {
      return i;
    }
  }

  return -1;
}

void InputManager::readButtonAdc(ButtonAdcSample& group1, ButtonAdcSample& group2) {
  group1 = {BUTTON_ADC_PIN_1, -1, -1};
  group2 = {BUTTON_ADC_PIN_2, -1, -1};
  if (BoardConfig::ACTIVE.inputStyle != BoardConfig::InputStyle::XteinkAdcLadder) {
    return;
  }

  group1.raw = analogRead(BUTTON_ADC_PIN_1);
  group1.button = getButtonFromADC(group1.raw, ADC_RANGES_1, NUM_BUTTONS_1);

  group2.raw = analogRead(BUTTON_ADC_PIN_2);
  const int b2 = getButtonFromADC(group2.raw, ADC_RANGES_2, NUM_BUTTONS_2);
  group2.button = b2 >= 0 ? b2 + 4 : -1;
}

uint8_t InputManager::getState() {
  uint8_t state = 0;

  if (BoardConfig::ACTIVE.inputStyle == BoardConfig::InputStyle::OnePageAdcLadder) {
    if (BoardConfig::ACTIVE.input.adcLadderPin >= 0) {
      const int mv = analogReadMilliVolts(BoardConfig::ACTIVE.input.adcLadderPin);
      if (mv >= 2400 && mv <= 2800)      state |= (1 << BTN_BACK);
      else if (mv >= 1780 && mv <= 2140) state |= (1 << BTN_LEFT);
      else if (mv >= 1140 && mv <= 1500) state |= (1 << BTN_RIGHT);
      else if (mv >= 0 && mv <= 250)     state |= (1 << BTN_CONFIRM);
    }
    if (BoardConfig::ACTIVE.input.up >= 0 && digitalRead(BoardConfig::ACTIVE.input.up) == LOW) {
      state |= (1 << BTN_UP);
    }
    if (BoardConfig::ACTIVE.input.down >= 0 && digitalRead(BoardConfig::ACTIVE.input.down) == LOW) {
      state |= (1 << BTN_DOWN);
    }
    if (BoardConfig::ACTIVE.input.power >= 0) {
      const int activeLevel = BoardConfig::ACTIVE.input.powerActiveHigh ? HIGH : LOW;
      if (digitalRead(BoardConfig::ACTIVE.input.power) == activeLevel) {
        state |= (1 << BTN_POWER);
      }
    }
    state |= serviceTouch();
    if (s_buttonHook) state |= s_buttonHook();
    return state;
  }

  if (BoardConfig::ACTIVE.inputStyle != BoardConfig::InputStyle::XteinkAdcLadder) {
    state = getDigitalState();
    state |= serviceTouch();
    if (s_buttonHook) state |= s_buttonHook();
    return state;
  }


  const int adcValue1 = analogRead(BUTTON_ADC_PIN_1);
  const int button1 = getButtonFromADC(adcValue1, ADC_RANGES_1, NUM_BUTTONS_1);
  if (button1 >= 0) {
    state |= (1 << button1);
  }


  const int adcValue2 = analogRead(BUTTON_ADC_PIN_2);
  const int button2 = getButtonFromADC(adcValue2, ADC_RANGES_2, NUM_BUTTONS_2);
  if (button2 >= 0) {
    state |= (1 << (button2 + 4));
  }


  const int powerActiveLevel = BoardConfig::ACTIVE.input.powerActiveHigh ? HIGH : LOW;
  if (digitalRead(BoardConfig::ACTIVE.input.power) == powerActiveLevel) {
    state |= (1 << BTN_POWER);
  }

  state |= serviceTouch();
  if (s_buttonHook) state |= s_buttonHook();
  return state;
}

InputManager::ButtonHook InputManager::s_buttonHook = nullptr;

#if FREEINK_DEVICE_PICCO
namespace {
uint16_t piccoReadReport(uint8_t addr, uint8_t* buf, uint16_t cap);
bool piccoWaitInt(int8_t irq, unsigned long timeoutMs);
}
#endif

bool InputManager::prepareForDeepSleep() {
#if FREEINK_DEVICE_EEGO_A4
  if (const auto& t = BoardConfig::ACTIVE.touch; t.controller == BoardConfig::TouchController::Gslx680) {
    waitForTouchInit();
    const uint8_t halt = 0x88;
    const bool ok = gslWrite(0xE0, &halt, 1);
    delay(5);
    {
      const freeink::I2cBusLock busLock;
      Wire.end();
    }
    if (t.sda >= 0) pinMode(t.sda, INPUT);
    if (t.scl >= 0) pinMode(t.scl, INPUT);
    if (t.reset >= 0) {
      const auto reset = static_cast<gpio_num_t>(t.reset);
      gpio_hold_dis(reset);
      pinMode(t.reset, OUTPUT);
      digitalWrite(t.reset, LOW);
      gpio_hold_en(reset);
    }
    touchDataEnabled = false;
    return ok;
  }
#endif
#if FREEINK_DEVICE_PICCO
  if (const auto& t = BoardConfig::ACTIVE.touch;
      t.controller == BoardConfig::TouchController::Tma525c && t.i2cAddress != 0) {



    static const uint8_t sleepCmd[] = {0x05, 0x00, 0x01, 0x08};
    Wire.beginTransmission(t.i2cAddress);
    Wire.write(sleepCmd, sizeof(sleepCmd));
    const bool ok = Wire.endTransmission() == 0;
    uint8_t buf[16];
    if (ok && piccoWaitInt(t.irq, 500)) piccoReadReport(t.i2cAddress, buf, sizeof(buf));
    if (t.irq >= 0) detachInterrupt(t.irq);
    for (const int8_t pin : {t.reset, t.irq, int8_t(12)}) {
      if (pin < 0) continue;
      const auto g = static_cast<gpio_num_t>(pin);
      rtc_gpio_init(g);
      rtc_gpio_set_direction(g, RTC_GPIO_MODE_INPUT_ONLY);
      rtc_gpio_pullup_dis(g);
      rtc_gpio_pulldown_en(g);
    }
    touchDataEnabled = false;
    return ok;
  }
#endif
  return true;
}

void InputManager::beginAsync(const uint8_t taskPriority, const uint32_t pollMs, const uint8_t queueLen) {
  if (_asyncTask) return;
  _asyncPollMs = pollMs;
  _asyncQueue = xQueueCreate(queueLen, sizeof(uint8_t));
  if (!_asyncQueue) return;
  _asyncTapQueue = xQueueCreate(queueLen, sizeof(float) * 2);
  _asyncSwipeQueue = xQueueCreate(queueLen, sizeof(float) * 4);
  _asyncMultiTouchSwipeQueue = xQueueCreate(queueLen, sizeof(QueuedMultiTouchSwipe));
  _asyncMultiTouchRotationQueue = xQueueCreate(queueLen, sizeof(QueuedMultiTouchRotation));
  _asyncMultiTouchPinchQueue = xQueueCreate(queueLen, sizeof(QueuedMultiTouchPinch));
  xTaskCreate(asyncTaskTrampoline, "fi_input", 4096, this, taskPriority, &_asyncTask);
}

void InputManager::asyncTaskTrampoline(void* self) { static_cast<InputManager*>(self)->asyncPoll(); }

void InputManager::asyncPoll() {
  static const uint8_t kButtons[] = {BTN_BACK, BTN_CONFIRM, BTN_LEFT, BTN_RIGHT, BTN_UP, BTN_DOWN, BTN_POWER};
  for (;;) {
    update();
    for (const uint8_t b : kButtons) {
      if (wasPressed(b)) xQueueSend(_asyncQueue, &b, 0);
    }
    float tap[2];
    if (_asyncTapQueue && wasTouchTap(tap[0], tap[1])) {
      xQueueSend(_asyncTapQueue, tap, 0);
    }
    float swipe[4];
    if (_asyncSwipeQueue && wasSwipe(swipe[0], swipe[1], swipe[2], swipe[3])) {
      xQueueSend(_asyncSwipeQueue, swipe, 0);
    }
    if (_asyncMultiTouchSwipeQueue && multiTouchSwipeEvent && !touchSuppressed) {
      const QueuedMultiTouchSwipe multiTouchSwipe = {multiTouchSwipeStartX,       multiTouchSwipeStartY,
                                                     multiTouchSwipeEndX,         multiTouchSwipeEndY,
                                                     multiTouchSwipeContactCount, multiTouchSwipeDurationMs};
      xQueueSend(_asyncMultiTouchSwipeQueue, &multiTouchSwipe, 0);
    }
    if (_asyncMultiTouchRotationQueue && multiTouchRotationEvent && !touchSuppressed) {
      const QueuedMultiTouchRotation rotation = {multiTouchRotationDegrees, multiTouchRotationCenterX,
                                                 multiTouchRotationCenterY, multiTouchRotationDurationMs};
      xQueueSend(_asyncMultiTouchRotationQueue, &rotation, 0);
    }
    if (_asyncMultiTouchPinchQueue && multiTouchPinchEvent && !touchSuppressed) {
      const QueuedMultiTouchPinch pinch = {multiTouchPinchScale, multiTouchPinchCenterX, multiTouchPinchCenterY,
                                           multiTouchPinchDurationMs};
      xQueueSend(_asyncMultiTouchPinchQueue, &pinch, 0);
    }
    vTaskDelay(pdMS_TO_TICKS(_asyncPollMs));
  }
}

bool InputManager::popPress(uint8_t& button) {
  if (!_asyncQueue) return false;
  return xQueueReceive(_asyncQueue, &button, 0) == pdTRUE;
}

bool InputManager::popTouchTap(float& nx, float& ny) {
  if (!_asyncTapQueue) return false;
  float tap[2];
  if (xQueueReceive(_asyncTapQueue, tap, 0) != pdTRUE) return false;
  nx = tap[0];
  ny = tap[1];
  return true;
}

bool InputManager::popSwipe(float& nxStart, float& nyStart, float& nxEnd, float& nyEnd) {
  if (!_asyncSwipeQueue) return false;
  float swipe[4];
  if (xQueueReceive(_asyncSwipeQueue, swipe, 0) != pdTRUE) return false;
  nxStart = swipe[0];
  nyStart = swipe[1];
  nxEnd = swipe[2];
  nyEnd = swipe[3];
  return true;
}

bool InputManager::popMultiTouchSwipe(uint8_t& contactCount, float& nxStart, float& nyStart, float& nxEnd, float& nyEnd,
                                      unsigned long& durationMs) {
  if (!_asyncMultiTouchSwipeQueue) return false;
  QueuedMultiTouchSwipe swipe{};
  if (xQueueReceive(_asyncMultiTouchSwipeQueue, &swipe, 0) != pdTRUE) return false;
  contactCount = swipe.contactCount;
  normalizeTouchPoint(swipe.startX, swipe.startY, nxStart, nyStart);
  normalizeTouchPoint(swipe.endX, swipe.endY, nxEnd, nyEnd);
  durationMs = swipe.durationMs;
  return true;
}

bool InputManager::popMultiTouchRotation(float& degrees, float& nxCenter, float& nyCenter, unsigned long& durationMs) {
  if (!_asyncMultiTouchRotationQueue) return false;
  QueuedMultiTouchRotation rotation{};
  if (xQueueReceive(_asyncMultiTouchRotationQueue, &rotation, 0) != pdTRUE) return false;
  degrees = rotation.degrees;
  normalizeTouchPoint(rotation.centerX, rotation.centerY, nxCenter, nyCenter);
  durationMs = rotation.durationMs;
  return true;
}

bool InputManager::popMultiTouchPinch(float& scale, float& nxCenter, float& nyCenter, unsigned long& durationMs) {
  if (!_asyncMultiTouchPinchQueue) return false;
  QueuedMultiTouchPinch pinch{};
  if (xQueueReceive(_asyncMultiTouchPinchQueue, &pinch, 0) != pdTRUE) return false;
  scale = pinch.scale;
  normalizeTouchPoint(pinch.centerX, pinch.centerY, nxCenter, nyCenter);
  durationMs = pinch.durationMs;
  return true;
}

bool InputManager::isDigitalPressed(const int8_t pin) const { return pin >= 0 && digitalRead(pin) == LOW; }

uint8_t InputManager::getDigitalState() const {
  uint8_t state = 0;
#if FREEINK_DEVICE_METALIO_EINK4
  if (BoardConfig::isMetalioEInk4()) {
    const uint16_t pins = freeink::metalio::readButtons();
    if (!(pins & (1u << freeink::metalio::PIN_VOLUME_UP))) state |= (1 << BTN_UP);
    if (!(pins & (1u << freeink::metalio::PIN_VOLUME_DOWN))) state |= (1 << BTN_DOWN);
  }
#endif

  if (BoardConfig::ACTIVE.inputStyle != BoardConfig::InputStyle::DigitalConfirmBackHold &&
      BoardConfig::ACTIVE.inputStyle != BoardConfig::InputStyle::DigitalConfirmPowerHold) {
    if (isDigitalPressed(BoardConfig::ACTIVE.input.back)) state |= (1 << BTN_BACK);
    if (isDigitalPressed(BoardConfig::ACTIVE.input.confirm)) state |= (1 << BTN_CONFIRM);
  }

  if (isDigitalPressed(BoardConfig::ACTIVE.input.left)) state |= (1 << BTN_LEFT);
  if (isDigitalPressed(BoardConfig::ACTIVE.input.right)) state |= (1 << BTN_RIGHT);
  if (isDigitalPressed(BoardConfig::ACTIVE.input.up)) state |= (1 << BTN_UP);
  if (isDigitalPressed(BoardConfig::ACTIVE.input.down)) state |= (1 << BTN_DOWN);

  if (isPowerButtonPhysicallyPressed() &&
      BoardConfig::ACTIVE.inputStyle != BoardConfig::InputStyle::DigitalConfirmBackHold &&
      BoardConfig::ACTIVE.inputStyle != BoardConfig::InputStyle::DigitalConfirmPowerHold) {
    state |= (1 << BTN_POWER);
  }

  return state;
}

void InputManager::applyStateChange(const uint8_t state, const unsigned long currentTime) {
  pressedEvents = state & ~currentState;
  releasedEvents = currentState & ~state;

  if (pressedEvents > 0 && currentState == 0) {
    buttonPressStart = currentTime;
  }

  if (releasedEvents > 0 && state == 0) {
    buttonPressFinish = currentTime;
  }

  if (pressedEvents & (1 << BTN_POWER)) {
    powerButtonPressStart = currentTime;
  }

  if (releasedEvents & (1 << BTN_POWER)) {
    powerButtonPressFinish = currentTime;
  }

  currentState = state;




  lastState = state;
}

void InputManager::updateConfirmBackHold(const unsigned long currentTime) {
  const bool pressed = isDigitalPressed(BoardConfig::ACTIVE.input.confirm);
  const uint8_t nonSharedState = getDigitalState();
  bool emitConfirmClick = false;

  if (pressed && !confirmBackPhysicalPressed) {
    confirmBackPhysicalPressed = true;
    confirmBackLongPressActive = false;
    confirmBackPressStart = currentTime;
  }

  uint8_t nextState = nonSharedState;
  if (pressed && currentTime - confirmBackPressStart >= CONFIRM_BACK_HOLD_MS) {
    confirmBackLongPressActive = true;
    nextState |= (1 << BTN_BACK);
  }

  if (!pressed && confirmBackPhysicalPressed) {
    confirmBackPhysicalPressed = false;
    if (!confirmBackLongPressActive) {
      emitConfirmClick = true;
      buttonPressStart = confirmBackPressStart;
      buttonPressFinish = currentTime;
    }
    confirmBackLongPressActive = false;
  }

  applyStateChange(nextState, currentTime);

  if (emitConfirmClick) {
    pressedEvents |= (1 << BTN_CONFIRM);
    releasedEvents |= (1 << BTN_CONFIRM);
  }
}

void InputManager::updateConfirmPowerHold(const unsigned long currentTime) {
  const int8_t sharedPin =
      BoardConfig::ACTIVE.input.confirm >= 0 ? BoardConfig::ACTIVE.input.confirm : BoardConfig::ACTIVE.input.power;
  const bool pressed = isDigitalPressed(sharedPin);
  uint8_t nonSharedState = getDigitalState();
  nonSharedState |= serviceTouch();
  if (s_buttonHook) nonSharedState |= s_buttonHook();
  bool emitConfirmClick = false;

  if (pressed && !confirmPowerPhysicalPressed) {
    confirmPowerPhysicalPressed = true;
    confirmPowerLongPressActive = false;
    confirmPowerPressStart = currentTime;
  }

  uint8_t nextState = nonSharedState;
  if (pressed && s_sharedConfirmPowerShortPressEmitsPower) {
    nextState |= (1 << BTN_POWER);
  } else if (pressed && currentTime - confirmPowerPressStart >= CONFIRM_POWER_HOLD_MS) {
    confirmPowerLongPressActive = true;
    nextState |= (1 << BTN_POWER);
  }

  if (!pressed && confirmPowerPhysicalPressed) {
    confirmPowerPhysicalPressed = false;
    if (!confirmPowerLongPressActive) {
      if (!s_sharedConfirmPowerShortPressEmitsPower) {
        emitConfirmClick = true;
      }
      buttonPressStart = confirmPowerPressStart;
      buttonPressFinish = currentTime;
    }
    confirmPowerLongPressActive = false;
  }

  applyStateChange(nextState, currentTime);

  if (pressedEvents & (1 << BTN_POWER)) {
    powerButtonPressStart = confirmPowerPressStart;
  }

  if (emitConfirmClick) {
    pressedEvents |= (1 << BTN_CONFIRM);
    releasedEvents |= (1 << BTN_CONFIRM);
  }
}

void InputManager::updateDigitalTwoButton(const unsigned long currentTime) {
  const bool up = isDigitalPressed(BoardConfig::ACTIVE.input.up);
  const bool down = isDigitalPressed(BoardConfig::ACTIVE.input.down);
  const uint8_t physical = static_cast<uint8_t>((up ? 1u : 0u) | (down ? 2u : 0u));
  uint8_t auxiliaryState = serviceTouch();
  if (s_buttonHook) auxiliaryState |= s_buttonHook();
#if FREEINK_DEVICE_PAPERMONO





  if (freeink::papermono::pollPowerButtonClicked(currentTime)) {
    auxiliaryState |= static_cast<uint8_t>(1u << BTN_POWER);
  }
#endif

  if (physical != twoButtonPhysicalState) {
    const uint8_t releasedPhysical = twoButtonPhysicalState;
    const bool emitShort = physical == 0 && !twoButtonLongPressActive;

    applyStateChange(auxiliaryState, currentTime);
    if (emitShort && (releasedPhysical == 1 || releasedPhysical == 2)) {
      const uint8_t logical = releasedPhysical == 1 ? BTN_UP : BTN_DOWN;
      pressedEvents |= static_cast<uint8_t>(1u << logical);
      releasedEvents |= static_cast<uint8_t>(1u << logical);
      buttonPressStart = twoButtonPressStart;
      buttonPressFinish = currentTime;
    }

    twoButtonPhysicalState = physical;
    twoButtonPressStart = currentTime;
    twoButtonLongPressActive = false;
    return;
  }

  if (physical == 0) {
    applyStateChange(auxiliaryState, currentTime);
    return;
  }

  uint8_t nextState = auxiliaryState;
  if (currentTime - twoButtonPressStart >= TWO_BUTTON_HOLD_MS) {
    twoButtonLongPressActive = true;
    const uint8_t logical = physical == 1 ? BTN_BACK : physical == 2 ? BTN_CONFIRM : BTN_POWER;
    nextState |= static_cast<uint8_t>(1u << logical);
  }
  applyStateChange(nextState, currentTime);
  if (pressedEvents & (1u << BTN_POWER)) powerButtonPressStart = twoButtonPressStart;
}

void InputManager::update() {
  const unsigned long currentTime = millis();

  pressedEvents = 0;
  releasedEvents = 0;
  touchPressedEvent = false;
  touchReleasedEvent = false;
  touchLongPressEvent = false;
  multiTouchSwipeEvent = false;
  multiTouchRotationEvent = false;
  multiTouchPinchEvent = false;
  touchHomeKeyEvent = false;
  touchHomeKeyTapEvent = false;
  touchHomeKeyLongEvent = false;
  updateToggleSwitch(currentTime);

  if (BoardConfig::ACTIVE.inputStyle == BoardConfig::InputStyle::DigitalConfirmBackHold) {
    updateConfirmBackHold(currentTime);
    return;
  }
  if (BoardConfig::ACTIVE.inputStyle == BoardConfig::InputStyle::DigitalConfirmPowerHold) {
    updateConfirmPowerHold(currentTime);
    return;
  }
  if (BoardConfig::ACTIVE.inputStyle == BoardConfig::InputStyle::DigitalTwoButton) {
    updateDigitalTwoButton(currentTime);
    return;
  }

  const uint8_t state = getState();


  if (state != lastState) {
    lastDebounceTime = currentTime;
    lastState = state;
  }

  if ((currentTime - lastDebounceTime) > DEBOUNCE_DELAY) {
    if (state != currentState) {
      applyStateChange(state, currentTime);
    }
  }
}

void InputManager::updateToggleSwitch(const unsigned long now) {
  toggleSwitchChangedEvent = false;
  if (!hasToggleSwitch()) return;
  const bool raw = digitalRead(BoardConfig::ACTIVE.input.toggleSwitch) == LOW;
  if (raw != toggleSwitchRaw) {
    toggleSwitchRaw = raw;
    toggleSwitchRawSince = now;
  } else if (raw != toggleSwitchOn && now - toggleSwitchRawSince > DEBOUNCE_DELAY) {
    toggleSwitchOn = raw;
    toggleSwitchChangedEvent = true;
  }
}

bool InputManager::isPressed(const uint8_t buttonIndex) const { return currentState & (1 << buttonIndex); }

bool InputManager::isPowerButtonPhysicallyPressed() const {
  const int8_t pin = BoardConfig::ACTIVE.input.power;
  if (pin < 0) return false;
  const int activeLevel = BoardConfig::ACTIVE.input.powerActiveHigh ? HIGH : LOW;
  return digitalRead(pin) == activeLevel;
}

bool InputManager::wasPressed(const uint8_t buttonIndex) const { return pressedEvents & (1 << buttonIndex); }

bool InputManager::wasAnyPressed() const { return pressedEvents > 0; }

bool InputManager::wasReleased(const uint8_t buttonIndex) const { return releasedEvents & (1 << buttonIndex); }

bool InputManager::wasAnyReleased() const { return releasedEvents > 0; }

unsigned long InputManager::getHeldTime() const {

  if (currentState > 0) {
    return millis() - buttonPressStart;
  }

  return buttonPressFinish - buttonPressStart;
}

unsigned long InputManager::getPowerButtonHeldTime() const {
  if (isPressed(BTN_POWER)) {
    return millis() - powerButtonPressStart;
  }

  return powerButtonPressFinish - powerButtonPressStart;
}

const char* InputManager::getButtonName(const uint8_t buttonIndex) {
  if (buttonIndex <= BTN_POWER) {
    return BUTTON_NAMES[buttonIndex];
  }
  return "Unknown";
}

bool InputManager::s_sharedConfirmPowerShortPressEmitsPower = false;

bool InputManager::isPowerButtonPressed() const { return isPressed(BTN_POWER); }















bool InputManager::hasTouch() const {
#if FREEINK_CAP_TOUCH
  return touchDataEnabled;
#else
  return false;
#endif
}

int8_t InputManager::touchWakeIrqPin() const {
#if FREEINK_TOUCH_GT911 && FREEINK_GT911_INT_WAKE
  return touchWakeIrqUsable && !gt911StatusFault ? BoardConfig::ACTIVE.touch.irq : static_cast<int8_t>(-1);
#else
  return -1;
#endif
}

bool InputManager::touchWakeIrqActiveLow() const {
#if FREEINK_CAP_TOUCH




  return BoardConfig::ACTIVE.touch.irqActiveLow;
#else
  return true;
#endif
}

InputManager::TouchPoint InputManager::getTouchPoint() const { return touchPoint; }

bool InputManager::supportsMultiTouch() const {
#if FREEINK_CAP_TOUCH
  if (!touchDataEnabled) return false;
#if FREEINK_TOUCH_TMA525C
  if (BoardConfig::ACTIVE.touch.controller == BoardConfig::TouchController::Tma525c) return piccoMaxContacts >= 2;
#endif
  const auto controller = BoardConfig::ACTIVE.touch.controller;
  return controller == BoardConfig::TouchController::Gt911 || controller == BoardConfig::TouchController::Gslx680;
#else
  return false;
#endif
}

InputManager::TouchSnapshot InputManager::getTouchSnapshot() const {
#if FREEINK_CAP_TOUCH
  return touchSnapshot;
#else
  return {};
#endif
}

bool InputManager::isTouchPressed() const { return touchPressed; }
bool InputManager::wasTouchPressed() const { return touchPressedEvent && !touchMultiContactSequence; }
bool InputManager::wasTouchReleased() const {



  return touchReleasedEvent && (!touchSuppressed || touchMultiContactSequence);
}

void InputManager::normalizeTouchPoint(const uint16_t x, const uint16_t y, float& nx, float& ny) const {
  const auto& t = BoardConfig::ACTIVE.touch;
  const uint16_t w = (t.rawMaxX > t.rawMinX) ? static_cast<uint16_t>(t.rawMaxX - t.rawMinX) : 1;
  const uint16_t h = (t.rawMaxY > t.rawMinY) ? static_cast<uint16_t>(t.rawMaxY - t.rawMinY) : 1;
  const auto clamp01 = [](const float value) { return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value); };
  nx = clamp01(static_cast<float>(x) / w);
  ny = clamp01(static_cast<float>(y) / h);
}

bool InputManager::wasTouchTap(float& nx, float& ny) const {
#if FREEINK_CAP_TOUCH
  if (!touchReleasedEvent || touchSuppressed || touchMultiContactSequence) return false;




  if (touchMovedBeyondTapReleaseSlop) return false;




  normalizeTouchPoint(touchDownPoint.x, touchDownPoint.y, nx, ny);
  return true;
#else
  (void)nx;
  (void)ny;
  return false;
#endif
}

bool InputManager::wasTouchPressedAt(float& nx, float& ny) const {
#if FREEINK_CAP_TOUCH




  if (!touchPressedEvent || touchMultiContactSequence) return false;
  normalizeTouchPoint(touchDownPoint.x, touchDownPoint.y, nx, ny);
  return true;
#else
  (void)nx;
  (void)ny;
  return false;
#endif
}

bool InputManager::isTouchTapCandidate(float& nx, float& ny, unsigned long& heldMs) const {
#if FREEINK_CAP_TOUCH
  if (!touchPressed || touchMovedBeyondTapSlop || touchSuppressed || touchMultiContactSequence) return false;
  normalizeTouchPoint(touchDownPoint.x, touchDownPoint.y, nx, ny);
  heldMs = millis() - touchDownPoint.timestamp;
  return true;
#else
  (void)nx;
  (void)ny;
  (void)heldMs;
  return false;
#endif
}

bool InputManager::isTouchHeldAt(float& nx, float& ny) const {
#if FREEINK_CAP_TOUCH


  if (!touchPressed || touchSuppressed || touchMultiContactSequence) return false;
  normalizeTouchPoint(touchUpPoint.x, touchUpPoint.y, nx, ny);
  return true;
#else
  (void)nx;
  (void)ny;
  return false;
#endif
}

unsigned long InputManager::lastTouchHeldMs() const {
#if FREEINK_CAP_TOUCH
  return lastTouchHeldDurationMs;
#else
  return 0;
#endif
}

bool InputManager::wasTouchActivity() const {
#if FREEINK_CAP_TOUCH
  const bool screenActivity = touchPressedEvent || touchReleasedEvent;
  const bool homeKeyActivity = touchHomeKeyEvent || touchHomeKeyTapEvent || touchHomeKeyLongEvent;


  return screenActivity || (!touchPressed && homeKeyActivity);
#else
  return false;
#endif
}

bool InputManager::wasSwipe(float& nxStart, float& nyStart, float& nxEnd, float& nyEnd) const {
#if FREEINK_CAP_TOUCH
  if (!touchReleasedEvent || touchSuppressed || touchMultiContactSequence) return false;



  if (lastTouchHeldDurationMs > TOUCH_SWIPE_MAX_MS) return false;
  const int dx = static_cast<int>(touchUpPoint.x) - static_cast<int>(touchDownPoint.x);
  const int dy = static_cast<int>(touchUpPoint.y) - static_cast<int>(touchDownPoint.y);
  const int adx = absInt(dx);
  const int ady = absInt(dy);
  if (adx < TOUCH_SWIPE_MIN_PX && ady < TOUCH_SWIPE_MIN_PX) return false;
  normalizeTouchPoint(touchDownPoint.x, touchDownPoint.y, nxStart, nyStart);
  normalizeTouchPoint(touchUpPoint.x, touchUpPoint.y, nxEnd, nyEnd);
  return true;
#else
  (void)nxStart;
  (void)nyStart;
  (void)nxEnd;
  (void)nyEnd;
  return false;
#endif
}

bool InputManager::wasMultiTouchSwipe(uint8_t& contactCount, float& nxStart, float& nyStart, float& nxEnd, float& nyEnd,
                                      unsigned long& durationMs) const {
#if FREEINK_CAP_TOUCH
  if (!multiTouchSwipeEvent || touchSuppressed) return false;
  contactCount = multiTouchSwipeContactCount;
  normalizeTouchPoint(multiTouchSwipeStartX, multiTouchSwipeStartY, nxStart, nyStart);
  normalizeTouchPoint(multiTouchSwipeEndX, multiTouchSwipeEndY, nxEnd, nyEnd);
  durationMs = multiTouchSwipeDurationMs;
  return true;
#else
  (void)contactCount;
  (void)nxStart;
  (void)nyStart;
  (void)nxEnd;
  (void)nyEnd;
  (void)durationMs;
  return false;
#endif
}

bool InputManager::wasMultiTouchRotation(float& degrees, float& nxCenter, float& nyCenter,
                                         unsigned long& durationMs) const {
#if FREEINK_CAP_TOUCH
  if (!multiTouchRotationEvent || touchSuppressed) return false;
  degrees = multiTouchRotationDegrees;
  normalizeTouchPoint(multiTouchRotationCenterX, multiTouchRotationCenterY, nxCenter, nyCenter);
  durationMs = multiTouchRotationDurationMs;
  return true;
#else
  (void)degrees;
  (void)nxCenter;
  (void)nyCenter;
  (void)durationMs;
  return false;
#endif
}

bool InputManager::wasMultiTouchPinch(float& scale, float& nxCenter, float& nyCenter, unsigned long& durationMs) const {
#if FREEINK_CAP_TOUCH
  if (!multiTouchPinchEvent || touchSuppressed) return false;
  scale = multiTouchPinchScale;
  normalizeTouchPoint(multiTouchPinchCenterX, multiTouchPinchCenterY, nxCenter, nyCenter);
  durationMs = multiTouchPinchDurationMs;
  return true;
#else
  (void)scale;
  (void)nxCenter;
  (void)nyCenter;
  (void)durationMs;
  return false;
#endif
}

bool InputManager::wasTouchLongPress(float& nx, float& ny) const {
#if FREEINK_CAP_TOUCH
  if (!touchLongPressEvent || touchMultiContactSequence) return false;

  normalizeTouchPoint(touchDownPoint.x, touchDownPoint.y, nx, ny);
  return true;
#else
  (void)nx;
  (void)ny;
  return false;
#endif
}

void InputManager::suppressTouchContact() {
#if FREEINK_CAP_TOUCH


  if (touchPressed || touchReleasedEvent) touchSuppressed = true;
  cancelMultiTouchGesture();
  if (_asyncMultiTouchSwipeQueue) xQueueReset(_asyncMultiTouchSwipeQueue);
  if (_asyncMultiTouchRotationQueue) xQueueReset(_asyncMultiTouchRotationQueue);
  if (_asyncMultiTouchPinchQueue) xQueueReset(_asyncMultiTouchPinchQueue);
#endif
}

void InputManager::startMultiTouchGesture(const TouchSnapshot& snapshot, const unsigned long now) {
  if (snapshot.count < 2 || snapshot.count > MAX_TOUCH_CONTACTS || snapshot.reportedCount != snapshot.count) {
    blockMultiTouchGesture();
    return;
  }

  if (snapshot.idsStable) {
    for (uint8_t i = 0; i < snapshot.count; ++i) {
      for (uint8_t j = i + 1; j < snapshot.count; ++j) {
        if (snapshot.points[i].id == snapshot.points[j].id) {
          blockMultiTouchGesture();
          return;
        }
      }
    }
  }

  trackedTouchContactCount = snapshot.count;
  multiTouchRotationEligible = trackedTouchContactCount == 2;
  for (uint8_t i = 0; i < trackedTouchContactCount; ++i) {
    multiTouchContacts[i] = {snapshot.points[i].id, snapshot.points[i].point, snapshot.points[i].point};
    multiTouchContacts[i].start.timestamp = now;
  }
  for (uint8_t i = trackedTouchContactCount; i < MAX_TOUCH_CONTACTS; ++i) multiTouchContacts[i] = {};

  multiTouchGestureState = MultiTouchGestureState::Tracking;
  touchMultiContactSequence = true;

  touchMovedBeyondTapSlop = true;
  touchMovedBeyondTapReleaseSlop = true;
  touchLongPressEvent = false;
  touchLongPressFired = true;
}

void InputManager::blockMultiTouchGesture() {
  multiTouchGestureState = MultiTouchGestureState::Blocked;
  touchMultiContactSequence = true;
  touchMovedBeyondTapSlop = true;
  touchMovedBeyondTapReleaseSlop = true;
  touchLongPressEvent = false;
  touchLongPressFired = true;
}

void InputManager::resetMultiTouchGesture() {
  multiTouchGestureState = MultiTouchGestureState::Idle;
  trackedTouchContactCount = 0;
  multiTouchRotationEligible = false;
  touchMultiContactSequence = false;
  for (auto& contact : multiTouchContacts) contact = {};
}

void InputManager::cancelMultiTouchGesture() {
  multiTouchSwipeEvent = false;
  multiTouchRotationEvent = false;
  multiTouchPinchEvent = false;
  multiTouchGestureState =
      (touchPressed || touchReleasedEvent) ? MultiTouchGestureState::Blocked : MultiTouchGestureState::Idle;
}

bool InputManager::findContactAssignment(const TouchSnapshot& snapshot, const uint8_t trackedCount,
                                         uint8_t assignment[MAX_TOUCH_CONTACTS]) const {
  if (trackedCount == 0 || trackedCount > snapshot.count || snapshot.count > MAX_TOUCH_CONTACTS) return false;

  if (snapshot.idsStable) {
    uint8_t used = 0;
    for (uint8_t tracked = 0; tracked < trackedCount; ++tracked) {
      bool found = false;
      for (uint8_t current = 0; current < snapshot.count; ++current) {
        if ((used & (1u << current)) || snapshot.points[current].id != multiTouchContacts[tracked].id) continue;
        assignment[tracked] = current;
        used |= 1u << current;
        found = true;
        break;
      }
      if (!found) return false;
    }
    return true;
  }




  const auto squaredDistance = [](const TouchPoint& a, const TouchPoint& b) {
    const int64_t dx = static_cast<int64_t>(a.x) - static_cast<int64_t>(b.x);
    const int64_t dy = static_cast<int64_t>(a.y) - static_cast<int64_t>(b.y);
    return dx * dx + dy * dy;
  };

  uint16_t candidateCount = 1;
  for (uint8_t i = 0; i < trackedCount; ++i) candidateCount *= snapshot.count;
  int64_t bestDistance = INT64_MAX;
  int64_t secondDistance = INT64_MAX;
  uint8_t bestAssignment[MAX_TOUCH_CONTACTS] = {};
  for (uint16_t code = 0; code < candidateCount; ++code) {
    uint16_t remaining = code;
    uint8_t used = 0;
    uint8_t candidate[MAX_TOUCH_CONTACTS] = {};
    int64_t distance = 0;
    bool valid = true;
    for (uint8_t tracked = 0; tracked < trackedCount; ++tracked) {
      const uint8_t current = remaining % snapshot.count;
      remaining /= snapshot.count;
      if (used & (1u << current)) {
        valid = false;
        break;
      }
      used |= 1u << current;
      candidate[tracked] = current;
      distance += squaredDistance(multiTouchContacts[tracked].last, snapshot.points[current].point);
    }
    if (!valid) continue;
    if (distance < bestDistance) {
      secondDistance = bestDistance;
      bestDistance = distance;
      std::copy(candidate, candidate + trackedCount, bestAssignment);
    } else if (distance < secondDistance) {
      secondDistance = distance;
    }
  }

  if (bestDistance == INT64_MAX) return false;
  if (secondDistance != INT64_MAX && secondDistance - bestDistance <= TOUCH_CONTACT_ASSIGNMENT_AMBIGUITY_PX_SQ) {
    return false;
  }
  std::copy(bestAssignment, bestAssignment + trackedCount, assignment);
  return true;
}

bool InputManager::matchMultiTouchSnapshot(const TouchSnapshot& snapshot) {
  if (snapshot.count != trackedTouchContactCount) return false;
  uint8_t assignment[MAX_TOUCH_CONTACTS] = {};
  if (!findContactAssignment(snapshot, trackedTouchContactCount, assignment)) return false;
  for (uint8_t i = 0; i < trackedTouchContactCount; ++i) {
    multiTouchContacts[i].last = snapshot.points[assignment[i]].point;
  }
  return true;
}

bool InputManager::expandMultiTouchGesture(const TouchSnapshot& snapshot, const unsigned long now) {
  if (snapshot.count <= trackedTouchContactCount || snapshot.count > MAX_TOUCH_CONTACTS) return false;

  uint8_t assignment[MAX_TOUCH_CONTACTS] = {};
  if (!findContactAssignment(snapshot, trackedTouchContactCount, assignment)) return false;
  for (uint8_t i = 0; i < trackedTouchContactCount; ++i) {
    const TouchPoint& current = snapshot.points[assignment[i]].point;
    if (absInt(static_cast<int>(current.x) - multiTouchContacts[i].start.x) > TOUCH_TAP_SLOP_PX ||
        absInt(static_cast<int>(current.y) - multiTouchContacts[i].start.y) > TOUCH_TAP_SLOP_PX) {
      return false;
    }
  }




  startMultiTouchGesture(snapshot, now);
  return multiTouchGestureState == MultiTouchGestureState::Tracking;
}

bool InputManager::isTrackedContact(const MultiTouchPoint& point) const {
  for (uint8_t i = 0; i < trackedTouchContactCount; ++i) {
    if (point.id == multiTouchContacts[i].id) return true;
  }
  return false;
}

bool InputManager::hasStableTranslationGeometry() const {
  for (uint8_t first = 0; first < trackedTouchContactCount; ++first) {
    for (uint8_t second = first + 1; second < trackedTouchContactCount; ++second) {
      const int startSeparationX =
          static_cast<int>(multiTouchContacts[second].start.x) - multiTouchContacts[first].start.x;
      const int startSeparationY =
          static_cast<int>(multiTouchContacts[second].start.y) - multiTouchContacts[first].start.y;
      const int endSeparationX = static_cast<int>(multiTouchContacts[second].last.x) - multiTouchContacts[first].last.x;
      const int endSeparationY = static_cast<int>(multiTouchContacts[second].last.y) - multiTouchContacts[first].last.y;
      if (absInt(endSeparationX - startSeparationX) > TOUCH_MULTI_CONTACT_SEPARATION_SLOP_PX ||
          absInt(endSeparationY - startSeparationY) > TOUCH_MULTI_CONTACT_SEPARATION_SLOP_PX) {
        return false;
      }
    }
  }
  return true;
}

bool InputManager::hasEligibleRotationScale() const {
  if (trackedTouchContactCount != 2) return false;
  const auto toGesturePoint = [](const TouchPoint& point) {
    return freeink::input_detail::GesturePoint{point.x, point.y};
  };
  return freeink::input_detail::hasRotationScale(
      toGesturePoint(multiTouchContacts[0].start), toGesturePoint(multiTouchContacts[1].start),
      toGesturePoint(multiTouchContacts[0].last), toGesturePoint(multiTouchContacts[1].last));
}

bool InputManager::isMultiTouchTranslation(const unsigned long now) const {
  if (trackedTouchContactCount < 2 || now - multiTouchContacts[0].start.timestamp > TOUCH_MULTI_SWIPE_MAX_MS ||
      !hasStableTranslationGeometry()) {
    return false;
  }

  int startCenterX = 0;
  int startCenterY = 0;
  int endCenterX = 0;
  int endCenterY = 0;
  for (uint8_t i = 0; i < trackedTouchContactCount; ++i) {
    startCenterX += multiTouchContacts[i].start.x;
    startCenterY += multiTouchContacts[i].start.y;
    endCenterX += multiTouchContacts[i].last.x;
    endCenterY += multiTouchContacts[i].last.y;
  }
  startCenterX /= trackedTouchContactCount;
  startCenterY /= trackedTouchContactCount;
  endCenterX /= trackedTouchContactCount;
  endCenterY /= trackedTouchContactCount;
  const int centerDx = endCenterX - startCenterX;
  const int centerDy = endCenterY - startCenterY;
  const int centerAbsX = absInt(centerDx);
  const int centerAbsY = absInt(centerDy);

  if (centerAbsX >= TOUCH_SWIPE_MIN_PX && centerAbsX * 2 >= centerAbsY * 3) {
    for (uint8_t i = 0; i < trackedTouchContactCount; ++i) {
      const int dx = static_cast<int>(multiTouchContacts[i].last.x) - multiTouchContacts[i].start.x;
      if (absInt(dx) < TOUCH_SWIPE_MIN_PX || (dx > 0) != (centerDx > 0)) return false;
    }
    return true;
  }
  if (centerAbsY >= TOUCH_SWIPE_MIN_PX && centerAbsY * 2 >= centerAbsX * 3) {
    for (uint8_t i = 0; i < trackedTouchContactCount; ++i) {
      const int dy = static_cast<int>(multiTouchContacts[i].last.y) - multiTouchContacts[i].start.y;
      if (absInt(dy) < TOUCH_SWIPE_MIN_PX || (dy > 0) != (centerDy > 0)) return false;
    }
    return true;
  }
  return false;
}

bool InputManager::classifyMultiTouchRotation(const unsigned long now) {
  if (!multiTouchRotationEligible || trackedTouchContactCount != 2 ||
      now - multiTouchContacts[0].start.timestamp > TOUCH_MULTI_SWIPE_MAX_MS) {
    return false;
  }

  const auto toGesturePoint = [](const TouchPoint& point) {
    return freeink::input_detail::GesturePoint{point.x, point.y};
  };
  freeink::input_detail::RotationResult result;
  if (!freeink::input_detail::classifyRotation(
          toGesturePoint(multiTouchContacts[0].start), toGesturePoint(multiTouchContacts[1].start),
          toGesturePoint(multiTouchContacts[0].last), toGesturePoint(multiTouchContacts[1].last), result)) {
    return false;
  }

  multiTouchRotationDegrees = result.degrees;
  multiTouchRotationCenterX = result.centerX;
  multiTouchRotationCenterY = result.centerY;
  multiTouchRotationDurationMs = static_cast<uint16_t>(now - multiTouchContacts[0].start.timestamp);
  multiTouchRotationEvent = true;
  return true;
}

bool InputManager::classifyMultiTouchPinch(const unsigned long now) {
  if (trackedTouchContactCount != 2 || now - multiTouchContacts[0].start.timestamp > TOUCH_MULTI_SWIPE_MAX_MS) {
    return false;
  }

  const auto toGesturePoint = [](const TouchPoint& point) {
    return freeink::input_detail::GesturePoint{point.x, point.y};
  };
  freeink::input_detail::PinchResult result;
  if (!freeink::input_detail::classifyPinch(
          toGesturePoint(multiTouchContacts[0].start), toGesturePoint(multiTouchContacts[1].start),
          toGesturePoint(multiTouchContacts[0].last), toGesturePoint(multiTouchContacts[1].last), result)) {
    return false;
  }

  multiTouchPinchScale = result.scale;
  multiTouchPinchCenterX = result.centerX;
  multiTouchPinchCenterY = result.centerY;
  multiTouchPinchDurationMs = static_cast<uint16_t>(now - multiTouchContacts[0].start.timestamp);
  multiTouchPinchEvent = true;
  return true;
}

void InputManager::finishMultiTouchGesture(const unsigned long now) {
  if (classifyMultiTouchRotation(now)) {
    multiTouchGestureState = MultiTouchGestureState::Blocked;
    return;
  }
  if (classifyMultiTouchPinch(now)) {
    multiTouchGestureState = MultiTouchGestureState::Blocked;
    return;
  }
  if (isMultiTouchTranslation(now)) {
    uint32_t startX = 0;
    uint32_t startY = 0;
    uint32_t endX = 0;
    uint32_t endY = 0;
    for (uint8_t i = 0; i < trackedTouchContactCount; ++i) {
      startX += multiTouchContacts[i].start.x;
      startY += multiTouchContacts[i].start.y;
      endX += multiTouchContacts[i].last.x;
      endY += multiTouchContacts[i].last.y;
    }
    multiTouchSwipeContactCount = trackedTouchContactCount;
    multiTouchSwipeStartX = static_cast<uint16_t>(startX / trackedTouchContactCount);
    multiTouchSwipeStartY = static_cast<uint16_t>(startY / trackedTouchContactCount);
    multiTouchSwipeEndX = static_cast<uint16_t>(endX / trackedTouchContactCount);
    multiTouchSwipeEndY = static_cast<uint16_t>(endY / trackedTouchContactCount);
    multiTouchSwipeDurationMs = static_cast<uint16_t>(now - multiTouchContacts[0].start.timestamp);
    multiTouchSwipeEvent = true;
  }
  multiTouchGestureState = MultiTouchGestureState::Blocked;
}

void InputManager::updateMultiTouchGesture(const TouchSnapshot& snapshot, const unsigned long now) {
  if (snapshot.reportedCount > MAX_TOUCH_CONTACTS || snapshot.count != snapshot.reportedCount) {
    blockMultiTouchGesture();
    return;
  }
  if (snapshot.count == 0) {
    if (multiTouchGestureState == MultiTouchGestureState::Tracking) finishMultiTouchGesture(now);
    return;
  }
  if (multiTouchGestureState == MultiTouchGestureState::Blocked) return;
  if (multiTouchGestureState == MultiTouchGestureState::Idle) {
    if (snapshot.count < 2) return;


    if (touchPressed && (touchMovedBeyondTapSlop || touchLongPressFired)) {
      blockMultiTouchGesture();
      return;
    }
    startMultiTouchGesture(snapshot, now);
    return;
  }
  if (snapshot.count < trackedTouchContactCount) {


    if (snapshot.idsStable) {
      for (uint8_t i = 0; i < snapshot.count; ++i) {
        if (!isTrackedContact(snapshot.points[i])) {
          blockMultiTouchGesture();
          return;
        }
        for (uint8_t j = i + 1; j < snapshot.count; ++j) {
          if (snapshot.points[i].id == snapshot.points[j].id) {
            blockMultiTouchGesture();
            return;
          }
        }
      }
    }
    finishMultiTouchGesture(now);
    return;
  }
  if (snapshot.count > trackedTouchContactCount) {
    if (!expandMultiTouchGesture(snapshot, now)) blockMultiTouchGesture();
    return;
  }
  if (!matchMultiTouchSnapshot(snapshot)) {
    blockMultiTouchGesture();
    return;
  }
  if (multiTouchRotationEligible && !hasEligibleRotationScale()) multiTouchRotationEligible = false;
}

bool InputManager::wasHomeKeyPressed() const { return touchHomeKeyEvent; }

bool InputManager::wasHomeKeyTapped() const { return touchHomeKeyTapEvent; }

bool InputManager::wasHomeKeyLongPressed() const { return touchHomeKeyLongEvent; }

bool InputManager::isHomeKeyDown() const { return touchHomeKeyDown; }

void InputManager::beginTouch() {
#if FREEINK_CAP_TOUCH
  const auto& t = BoardConfig::ACTIVE.touch;
  if (t.controller == BoardConfig::TouchController::None) {
    return;
  }

#if FREEINK_TOUCH_CST816S
  if (t.controller == BoardConfig::TouchController::Cst816s) {
    beginCst816s();
    return;
  }
#endif
#if FREEINK_TOUCH_GT911
  if (t.controller == BoardConfig::TouchController::Gt911) {
    beginGt911();
    return;
  }
#endif
#if FREEINK_TOUCH_FT5X06
  if (t.controller == BoardConfig::TouchController::Ft5x06) {
    beginFt5x06();
    return;
  }
#endif
#if FREEINK_TOUCH_FT6336U
  if (t.controller == BoardConfig::TouchController::Ft6336u) {
    beginFt6336u();
    return;
  }
#endif
#if FREEINK_TOUCH_GSLX680
  if (t.controller == BoardConfig::TouchController::Gslx680) {
    beginGslx680();
    return;
  }
#endif
#if FREEINK_TOUCH_TMA525C
  if (t.controller == BoardConfig::TouchController::Tma525c) {
    beginTma525c();
    return;
  }
#endif
#if FREEINK_TOUCH_CHSC6X
  if (t.controller == BoardConfig::TouchController::Chsc6x) {



    if (t.sda >= 0 && t.scl >= 0 && t.i2cAddress != 0) {
      Wire.begin(t.sda, t.scl, 100000);
      Wire.setTimeOut(4);
      touchDataEnabled = true;
    }
    return;
  }
#endif



  static bool reportedMissingDriver = false;
  if (!reportedMissingDriver) {
    reportedMissingDriver = true;
    esp_rom_printf("[touch] no driver for touch controller %u in this build (FREEINK_TOUCH_*); touch disabled\n",
                   static_cast<unsigned>(t.controller));
  }
#endif
}

uint8_t InputManager::serviceTouch() {
#if FREEINK_CAP_TOUCH
  if (!touchDataEnabled) {
    return 0;
  }
  const unsigned long now = millis();
  const auto& t = BoardConfig::ACTIVE.touch;





  if (!touchPressed && !touchReleasedEvent) {
    touchSuppressed = false;
    touchLongPressFired = false;
    resetMultiTouchGesture();
  }

  switch (t.controller) {
#if FREEINK_TOUCH_CST816S
    case BoardConfig::TouchController::Cst816s:
      pollCst816s(now);
      break;
#endif
#if FREEINK_TOUCH_GT911
    case BoardConfig::TouchController::Gt911:
      pollGt911(now);
      break;
#endif
#if FREEINK_TOUCH_FT5X06
    case BoardConfig::TouchController::Ft5x06:
      pollFt5x06(now);
      break;
#endif
#if FREEINK_TOUCH_FT6336U
    case BoardConfig::TouchController::Ft6336u:
      pollFt6336u(now);
      break;
#endif
#if FREEINK_TOUCH_GSLX680
    case BoardConfig::TouchController::Gslx680:
      pollGslx680(now);
      break;
#endif
#if FREEINK_TOUCH_TMA525C
    case BoardConfig::TouchController::Tma525c:
      pollTma525c(now);
      break;
#endif
#if FREEINK_TOUCH_CHSC6X
    case BoardConfig::TouchController::Chsc6x:
      updateTouchFromIrq(now, 0);

      if (touchPressedEvent) touchIrqPulseUntil = now + TOUCH_IRQ_PULSE_MS;
      break;
#endif
    default:
      break;
  }



  if (touchPressed && !touchMultiContactSequence && !touchMovedBeyondTapSlop && !touchLongPressFired &&
      !touchSuppressed && now - touchDownPoint.timestamp >= TOUCH_LONG_PRESS_MS) {
    touchLongPressFired = true;
    touchLongPressEvent = true;
  }

#if FREEINK_TOUCH_CST816S
  if (t.controller == BoardConfig::TouchController::Cst816s) return cstVirtualButtons;
#endif
  return (t.synthesizeConfirm && now < touchIrqPulseUntil) ? (1 << BTN_CONFIRM) : 0;
#else
  return 0;
#endif
}

#if FREEINK_CAP_TOUCH

#if FREEINK_TOUCH_CHSC6X
void InputManager::updateTouchFromIrq(const unsigned long now, const int irqRaw) {







  (void)irqRaw;
  if (now >= touchReadAt) {
    touchReadAt = now + TOUCH_SAMPLE_DELAY_MS;
    TouchPoint point = {false, 0, 0, 0};
    if (readChsc6xPoint(point)) {
      touchPoint = point;
      if (!touchPressed) {
        touchPressed = true;
        touchPressedEvent = true;
        touchDownPoint = point;
        touchUpPoint = point;
        touchMovedBeyondTapSlop = false;
        touchMovedBeyondTapReleaseSlop = false;
      } else {
        touchUpPoint = point;
        const int dx = static_cast<int>(touchUpPoint.x) - static_cast<int>(touchDownPoint.x);
        const int dy = static_cast<int>(touchUpPoint.y) - static_cast<int>(touchDownPoint.y);
        if (absInt(dx) > TOUCH_TAP_SLOP_PX || absInt(dy) > TOUCH_TAP_SLOP_PX) {
          touchMovedBeyondTapSlop = true;
        }
        if (absInt(dx) > TOUCH_TAP_RELEASE_SLOP_PX || absInt(dy) > TOUCH_TAP_RELEASE_SLOP_PX) {
          touchMovedBeyondTapReleaseSlop = true;
        }
      }
      touchReleaseAt = now + TOUCH_IRQ_PULSE_MS;
    }
  }

  if (touchPressed && now >= touchReleaseAt) {
    touchPressed = false;
    touchReleasedEvent = true;
    lastTouchHeldDurationMs = now - touchDownPoint.timestamp;
  }
}

bool InputManager::readChsc6xPoint(TouchPoint& point) {
  const uint8_t addr = BoardConfig::ACTIVE.touch.i2cAddress;
  Wire.beginTransmission(addr);
  Wire.write(TOUCH_READ_COMMAND);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }

  uint8_t data[TOUCH_FRAME_SIZE] = {};
  const uint8_t received = Wire.requestFrom(addr, TOUCH_FRAME_SIZE, static_cast<uint8_t>(true));
  if (received != TOUCH_FRAME_SIZE) {
    while (Wire.available()) Wire.read();
    return false;
  }
  for (uint8_t i = 0; i < TOUCH_FRAME_SIZE; ++i) {
    data[i] = Wire.read();
  }
  return decodeChsc6xFrame(data, TOUCH_FRAME_SIZE, point);
}

bool InputManager::decodeChsc6xFrame(const uint8_t* data, const size_t len, TouchPoint& point) const {
  if (len < 7) {
    return false;
  }






  if ((data[3] & 0x80) == 0) {
    return false;
  }
  const uint16_t rawX = data[4];
  const uint16_t rawY = (static_cast<uint16_t>(data[5]) << 8) | data[6];
  if ((rawX == 0 && rawY == 0) || (rawX == 0xff && rawY == 0xffff)) {
    return false;
  }
  const auto& t = BoardConfig::ACTIVE.touch;
  point.valid = true;



  point.x = mapTouchAxis(rawX, t.rawMinX, t.rawMaxX, t.rawMaxX - t.rawMinX);
  point.y = mapTouchAxis(rawY, t.rawMinY, t.rawMaxY, t.rawMaxY - t.rawMinY);
  point.timestamp = millis();
  return true;
}
#endif

uint16_t InputManager::mapTouchAxis(uint16_t raw, const uint16_t rawMin, const uint16_t rawMax,
                                    const uint16_t outMax) const {
  if (raw <= rawMin) return 0;
  if (raw >= rawMax) return outMax;
  return static_cast<uint32_t>(raw - rawMin) * outMax / (rawMax - rawMin);
}



#if FREEINK_TOUCH_FT5X06 || FREEINK_TOUCH_CST816S
bool InputManager::ft5x06WriteReg(const uint8_t reg, const uint8_t value) {
  const freeink::I2cBusLock busLock;
  const uint8_t addr = BoardConfig::ACTIVE.touch.i2cAddress;
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool InputManager::ft5x06ReadReg(const uint8_t reg, uint8_t* buf, const uint8_t len) {
  const freeink::I2cBusLock busLock;
  const uint8_t addr = BoardConfig::ACTIVE.touch.i2cAddress;
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  const uint8_t got = Wire.requestFrom(addr, len, static_cast<uint8_t>(true));
  if (got != len) {
    while (Wire.available()) Wire.read();
    return false;
  }
  for (uint8_t i = 0; i < len; ++i) buf[i] = Wire.read();
  return true;
}
#endif

#if FREEINK_TOUCH_FT5X06
void InputManager::beginFt5x06() {
  const auto& t = BoardConfig::ACTIVE.touch;
  if (t.sda < 0 || t.scl < 0 || t.i2cAddress == 0) return;

#if FREEINK_DEVICE_PAPERMONO


  [[maybe_unused]] const bool railUp = freeink::papermono::enableTouch();
#ifdef TOUCH_PROBE_DEBUG
  touchDebugPrintf("[touch] Paper Mono rail and reset: %s\n", railUp ? "up" : "FAILED");
#endif
#endif




  Wire.begin(t.sda, t.scl, 100000);
  Wire.setTimeOut(10);
  if (t.irq >= 0) pinMode(t.irq, INPUT_PULLUP);










  uint8_t id[6] = {};
  bool wrMode = false, rdId = false, wrIrq = false;
  for (int attempt = 0; attempt < 12 && !rdId; ++attempt) {
    if (attempt) delay(50);
    wrMode = ft5x06WriteReg(0x00, 0x00);
    rdId = wrMode && ft5x06ReadReg(0xA3, id, sizeof(id));
    wrIrq = rdId && ft5x06WriteReg(0xA4, 0x00);
  }
  touchDataEnabled = wrMode && rdId && wrIrq;
#ifdef TOUCH_PROBE_DEBUG
#if FREEINK_DEVICE_PAPERMONO




  uint16_t ioeMode = 0xFFFF, ioeOut = 0xFFFF;
  freeink::m5ioe1::readReg16(freeink::m5ioe1::REG_GPIO_MODE_L, &ioeMode);
  freeink::m5ioe1::readReg16(freeink::m5ioe1::REG_GPIO_OUT_L, &ioeOut);
  touchDebugPrintf("[touch] IOE1 addr=0x%02X mode=0x%04X out=0x%04X\n", freeink::m5ioe1::g_addr, ioeMode, ioeOut);




  touchDebugPrintf("[touch] i2c scan:");
  for (uint8_t a = 0x08; a <= 0x77; ++a) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) touchDebugPrintf(" 0x%02X", a);
    delayMicroseconds(200);
  }
  touchDebugPrintf("\n");
#endif
  touchDebugPrintf(
      "[touch] FT5x06 probe: enabled=%d wrMode=%d rdId=%d wrIrq=%d "
      "cipher=0x%02X fw=0x%02X vendor=0x%02X irq=%d\n",
      touchDataEnabled, wrMode, rdId, wrIrq, id[0], id[3], id[5], t.irq);
#endif
}
#endif

#if FREEINK_TOUCH_CST816S



namespace {
volatile bool cstInterrupt = false;
void IRAM_ATTR cstTouchInterrupt() { cstInterrupt = true; }
}

void InputManager::beginCst816s() {
  const auto& t = BoardConfig::ACTIVE.touch;
#if FREEINK_DEVICE_METALIO_EINK4
  if (BoardConfig::isMetalioEInk4() && !freeink::metalio::ensureBooted()) return;
#endif
  if (t.sda < 0 || t.scl < 0 || !t.i2cAddress) return;
  if (!Wire.begin(t.sda, t.scl, 400000)) return;
  Wire.setTimeOut(10);
  cstVirtualButtons = 0;
  cstInterrupt = false;
  if (t.irq >= 0) {
    pinMode(t.irq, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(t.irq), cstTouchInterrupt, FALLING);
  }
  touchDataEnabled = true;
}

void InputManager::pollCst816s(const unsigned long now) {
  const auto& t = BoardConfig::ACTIVE.touch;
  if (now < touchReadAt) return;
  touchReadAt = now + TOUCH_SAMPLE_DELAY_MS;
  const bool active = touchPressed || touchHomeKeyDown || cstVirtualButtons;
  const bool fresh = cstInterrupt || t.irq < 0 || digitalRead(t.irq) == LOW;
  if (!active && !fresh) return;
  cstInterrupt = false;
  const auto release = [&]() {
    if (touchPressed) {
      touchPressed = false;
      touchPoint.valid = false;
      touchReleasedEvent = true;
      lastTouchHeldDurationMs = now - touchDownPoint.timestamp;
    }
    if (touchHomeKeyDown) {
      if (!touchHomeKeyLongFired) touchHomeKeyTapEvent = true;
      touchHomeKeyDown = false;
      touchHomeKeyLongFired = false;
    }
    cstVirtualButtons = 0;
  };


  uint8_t data[5] = {};
  if (!ft5x06ReadReg(0x02, data, sizeof(data))) {

    if (active && now - cstLastSample >= 100) release();
    return;
  }
  cstLastSample = now;
  if (!(data[0] & 0x0F) || (data[1] >> 6) == 1) {
    release();
    return;
  }
  const uint16_t rawX = ((data[1] & 0x0F) << 8) | data[2];
  const uint16_t rawY = ((data[3] & 0x0F) << 8) | data[4];
  if (BoardConfig::isMetalioEInk4() && rawY >= 800) {


    if (rawY < 860 || rawY > 940 || rawX > 479) { release(); return; }
    if (touchPressed) { suppressTouchContact(); release(); }
    if (rawX < 160) {
      cstVirtualButtons = 0;
      if (!touchHomeKeyDown) {
        touchHomeKeyDown = true;
        touchHomeKeyEvent = true;
        touchHomeKeyDownAt = now;
        touchHomeKeyLongFired = false;
      } else if (!touchHomeKeyLongFired && now - touchHomeKeyDownAt >= HOME_KEY_LONG_PRESS_MS) {
        touchHomeKeyLongEvent = true;
        touchHomeKeyLongFired = true;
      }
    } else {
      if (touchHomeKeyDown) release();
      cstVirtualButtons = (1 << (rawX < 320 ? BTN_DOWN : BTN_UP));
    }
    return;
  }
  if (touchHomeKeyDown || cstVirtualButtons) release();
  const uint16_t x = t.swapXY ? rawY : rawX;
  const uint16_t y = t.swapXY ? rawX : rawY;
  if (x < t.rawMinX || x > t.rawMaxX || y < t.rawMinY || y > t.rawMaxY) { release(); return; }
  touchPoint = {true, mapTouchAxis(x, t.rawMinX, t.rawMaxX, t.rawMaxX - t.rawMinX),
                     mapTouchAxis(y, t.rawMinY, t.rawMaxY, t.rawMaxY - t.rawMinY), now};
  if (t.flipX) touchPoint.x = t.rawMaxX - t.rawMinX - touchPoint.x;
  if (t.flipY) touchPoint.y = t.rawMaxY - t.rawMinY - touchPoint.y;
  if (!touchPressed) {
    touchPressed = true;
    touchPressedEvent = true;
    touchDownPoint = touchUpPoint = touchPoint;
    touchMovedBeyondTapSlop = touchMovedBeyondTapReleaseSlop = false;
  } else {
    touchUpPoint = touchPoint;
    const int dx = absInt(int(touchPoint.x) - int(touchDownPoint.x));
    const int dy = absInt(int(touchPoint.y) - int(touchDownPoint.y));
    if (dx > TOUCH_TAP_SLOP_PX || dy > TOUCH_TAP_SLOP_PX) touchMovedBeyondTapSlop = true;
    if (dx > TOUCH_TAP_RELEASE_SLOP_PX || dy > TOUCH_TAP_RELEASE_SLOP_PX) touchMovedBeyondTapReleaseSlop = true;
  }
}
#endif

#if FREEINK_TOUCH_FT5X06
void InputManager::pollFt5x06(const unsigned long now) {
  const auto& t = BoardConfig::ACTIVE.touch;
  if (now < touchReadAt) return;
  touchReadAt = now + TOUCH_SAMPLE_DELAY_MS;







  const bool irqDown = t.irq < 0 || digitalRead(t.irq) == LOW;
  if (!irqDown && !touchPressed) {
    return;
  }



  uint8_t data[5] = {};
  if (!ft5x06ReadReg(0x02, data, sizeof(data))) {



    constexpr unsigned long STALE_RELEASE_MS = 100;
    if (touchPressed && now - touchPoint.timestamp > STALE_RELEASE_MS) {
      touchPressed = false;
      touchPoint.valid = false;
      touchReleasedEvent = true;
      lastTouchHeldDurationMs = now - touchDownPoint.timestamp;
    }
    return;
  }
  if ((data[0] & 0x0F) == 0) {



    if (touchPressed) {
      touchPressed = false;
      touchPoint.valid = false;
      touchReleasedEvent = true;
      lastTouchHeldDurationMs = now - touchDownPoint.timestamp;
#ifdef TOUCH_PROBE_DEBUG
      touchDebugPrintf("[touch] FT release via TD_STATUS=0 held=%lums\n", lastTouchHeldDurationMs);
#endif
    }
    return;
  }
  const uint16_t rawX = static_cast<uint16_t>((data[1] & 0x0F) << 8) | data[2];
  const uint16_t rawY = static_cast<uint16_t>((data[3] & 0x0F) << 8) | data[4];
  const uint16_t sx = t.swapXY ? rawY : rawX;
  const uint16_t sy = t.swapXY ? rawX : rawY;

  touchPoint.valid = true;
  touchPoint.x = mapTouchAxis(sx, t.rawMinX, t.rawMaxX, t.rawMaxX - t.rawMinX);
  touchPoint.y = mapTouchAxis(sy, t.rawMinY, t.rawMaxY, t.rawMaxY - t.rawMinY);
  if (t.flipX) {
    touchPoint.x = static_cast<uint16_t>((t.rawMaxX - t.rawMinX) - touchPoint.x);
  }
  if (t.flipY) {
    touchPoint.y = static_cast<uint16_t>((t.rawMaxY - t.rawMinY) - touchPoint.y);
  }
  touchPoint.timestamp = now;

  if (!touchPressed) {
    touchPressed = true;
    touchPressedEvent = true;
    touchDownPoint = touchPoint;
    touchUpPoint = touchPoint;
    touchMovedBeyondTapSlop = false;
    touchMovedBeyondTapReleaseSlop = false;
#ifdef TOUCH_PROBE_DEBUG
    touchDebugPrintf("[touch] FT press raw=(%u,%u) panel=(%u,%u)\n", rawX, rawY, touchPoint.x, touchPoint.y);
#endif
  } else {
    touchUpPoint = touchPoint;
    const int dx = static_cast<int>(touchUpPoint.x) - static_cast<int>(touchDownPoint.x);
    const int dy = static_cast<int>(touchUpPoint.y) - static_cast<int>(touchDownPoint.y);
    if (absInt(dx) > TOUCH_TAP_SLOP_PX || absInt(dy) > TOUCH_TAP_SLOP_PX) {
      touchMovedBeyondTapSlop = true;
    }
    if (absInt(dx) > TOUCH_TAP_RELEASE_SLOP_PX || absInt(dy) > TOUCH_TAP_RELEASE_SLOP_PX) {
      touchMovedBeyondTapReleaseSlop = true;
    }
  }
}
#endif






#if FREEINK_TOUCH_GSLX680
bool InputManager::gslWrite(const uint8_t reg, const uint8_t* data, const uint8_t len) {
  const freeink::I2cBusLock busLock;
  const uint8_t addr = BoardConfig::ACTIVE.touch.i2cAddress;
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (len) Wire.write(data, len);
  return Wire.endTransmission() == 0;
}

bool InputManager::gslRead(const uint8_t reg, uint8_t* buf, const uint8_t len) {
  const freeink::I2cBusLock busLock;
  const uint8_t addr = BoardConfig::ACTIVE.touch.i2cAddress;
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  const uint8_t got = Wire.requestFrom(addr, len, static_cast<uint8_t>(true));
  if (got != len) {
    while (Wire.available()) Wire.read();
    return false;
  }
  for (uint8_t i = 0; i < len; ++i) buf[i] = Wire.read();
  return true;
}

bool InputManager::gslUploadFirmware() {
#if FREEINK_DEVICE_EEGO_A4
  size_t i = 0;
  while (i < freeink::EEGO_A4_GSL_FIRMWARE_LEN) {
    const freeink::Gslx680FwEntry& e = freeink::EEGO_A4_GSL_FIRMWARE[i];
    if (e.reg == 0xF0) {
      const uint8_t page = static_cast<uint8_t>(e.value & 0xFF);
      if (!gslWrite(0xF0, &page, 1)) return false;
      ++i;
      continue;
    }

    uint8_t payload[64];
    const uint8_t firstReg = e.reg;
    uint8_t bytes = 0;
    while (i < freeink::EEGO_A4_GSL_FIRMWARE_LEN && bytes < sizeof(payload)) {
      const auto& word = freeink::EEGO_A4_GSL_FIRMWARE[i];
      if (word.reg == 0xF0 || word.reg != firstReg + bytes) break;
      for (uint8_t shift = 0; shift < 32; shift += 8) payload[bytes++] = static_cast<uint8_t>(word.value >> shift);
      ++i;
    }
    if (!gslWrite(firstReg, payload, bytes)) return false;
  }
  return true;
#else
  return false;
#endif
}






void InputManager::beginGslx680() {
#if FREEINK_DEVICE_EEGO_A4
  touchDataEnabled = false;
  const auto& t = BoardConfig::ACTIVE.touch;
  if (t.sda < 0 || t.scl < 0 || t.i2cAddress == 0) return;
  freeink::enableI2cBusLock();
  {
    const freeink::I2cBusLock busLock;
    Wire.begin(t.sda, t.scl, 400000);
    Wire.setTimeOut(10);
  }
  gslInitRunning = true;
  if (xTaskCreatePinnedToCore(&InputManager::gslInitTask, "gsl_init", 4096, this, 1, nullptr, 0) != pdPASS) {
    gslInitBody();
    gslInitRunning = false;
  }
#endif
}

void InputManager::gslInitTask(void* arg) {
  auto* self = static_cast<InputManager*>(arg);
  self->gslInitBody();
  __atomic_store_n(&self->gslInitRunning, false, __ATOMIC_RELEASE);
  vTaskDelete(nullptr);
}

void InputManager::waitForTouchInit() const {

  for (int waited = 0; waited < 3000 && __atomic_load_n(&gslInitRunning, __ATOMIC_ACQUIRE); waited += 5) delay(5);
}

void InputManager::gslInitBody() {
#if FREEINK_DEVICE_EEGO_A4
  const auto& t = BoardConfig::ACTIVE.touch;


  if (t.reset >= 0) {
    gpio_hold_dis(static_cast<gpio_num_t>(t.reset));
    pinMode(t.reset, OUTPUT);
    digitalWrite(t.reset, LOW);
    delay(20);
    digitalWrite(t.reset, HIGH);
    delay(50);
  }


  const uint8_t z4[4] = {0, 0, 0, 0};
  auto wr1 = [&](uint8_t reg, uint8_t v) { gslWrite(reg, &v, 1); };
  auto resetBlock = [&]() {
    wr1(0xE0, 0x88);
    delay(20);
    wr1(0x80, 0x03);
    delay(5);
    wr1(0xE4, 0x04);
    delay(5);
    wr1(0xE0, 0x00);
    delay(20);
  };
  auto clearRegs = [&]() {
    wr1(0xE0, 0x88);
    delay(20);
    wr1(0xE4, 0x04);
    delay(10);
    gslWrite(0xBC, z4, 4);
    delay(10);
  };
  auto startup = [&]() {
    wr1(0xE0, 0x00);
    delay(10);
  };


  uint8_t probe = 0;
  gslRead(0xF0, &probe, 1);
  delay(2);
  wr1(0xF0, 0x12);
  delay(2);
  const bool present = gslRead(0xF0, &probe, 1);
  if (!present) {
    LOG_ERR("INPUT", "GSLX680 presence probe failed");
    return;
  }

  resetBlock();
  clearRegs();
  clearRegs();
  resetBlock();
  clearRegs();
  if (!gslUploadFirmware()) {
    LOG_ERR("INPUT", "GSLX680 firmware upload failed");
    return;
  }
  startup();
  clearRegs();
  startup();


  delay(30);
  uint8_t chk[4] = {0, 0, 0, 0};
  bool loaded = gslRead(0xB0, chk, 4) && chk[0] == 0x5A && chk[1] == 0x5A && chk[2] == 0x5A && chk[3] == 0x5A;
  if (!loaded) {
    clearRegs();
    startup();
    loaded = gslRead(0xB0, chk, 4) && chk[0] == 0x5A && chk[1] == 0x5A && chk[2] == 0x5A && chk[3] == 0x5A;
  }
  __atomic_store_n(&touchDataEnabled, loaded, __ATOMIC_RELEASE);
  if (!loaded) LOG_ERR("INPUT", "GSLX680 firmware status check failed");
#ifdef TOUCH_PROBE_DEBUG
  touchDebugPrintf("[touch] GSLX680 probe present=%d loaded=%d chk=%02X%02X%02X%02X\n", present, loaded, chk[0], chk[1],
                   chk[2], chk[3]);
#endif
#endif
}





namespace {
struct GslCalPoint {
  int32_t rawX, rawY, screenX, screenY;
};
constexpr GslCalPoint GSL_CAL_CENTRE = {549, 337, 276, 306};
constexpr GslCalPoint GSL_CAL_CORNERS[4] = {
    {720, 94, 80, 168}, {694, 560, 472, 168}, {392, 558, 472, 444}, {374, 97, 80, 444}};

int32_t roundedDiv(int64_t num, int64_t den) {
  if (den < 0) {
    num = -num;
    den = -den;
  }
  return static_cast<int32_t>(num >= 0 ? (num + den / 2) / den : -((-num + den / 2) / den));
}
}

void InputManager::gslCalibrate(const uint16_t rawX, const uint16_t rawY, int16_t& screenX, int16_t& screenY) {
  const auto& c = GSL_CAL_CENTRE;
  const int64_t dx = static_cast<int32_t>(rawX) - c.rawX;
  const int64_t dy = static_cast<int32_t>(rawY) - c.rawY;

  int a = 3;
  int b = 0;
  for (int i = 0; i < 4; ++i) {
    const auto& p = GSL_CAL_CORNERS[i];
    const auto& q = GSL_CAL_CORNERS[(i + 1) % 4];
    const int64_t px = p.rawX - c.rawX, py = p.rawY - c.rawY;
    const int64_t qx = q.rawX - c.rawX, qy = q.rawY - c.rawY;
    if (px * dy - py * dx >= 0 && qy * dx - qx * dy >= 0) {
      a = i;
      b = (i + 1) % 4;
      break;
    }
  }
  const auto& p = GSL_CAL_CORNERS[a];
  const auto& q = GSL_CAL_CORNERS[b];
  const int64_t px = p.rawX - c.rawX, py = p.rawY - c.rawY;
  const int64_t qx = q.rawX - c.rawX, qy = q.rawY - c.rawY;
  const int64_t den = px * qy - qx * py;
  const int64_t wp = dx * qy - qx * dy;
  const int64_t wq = px * dy - dx * py;
  const int32_t x = c.screenX + roundedDiv(wp * (p.screenX - c.screenX) + wq * (q.screenX - c.screenX), den);
  const int32_t y = c.screenY + roundedDiv(wp * (p.screenY - c.screenY) + wq * (q.screenY - c.screenY), den);
  screenX = static_cast<int16_t>(x < 0 ? 0 : (x > 551 ? 551 : x));
  screenY = static_cast<int16_t>(y < 0 ? 0 : (y > 767 ? 767 : y));
}

void InputManager::pollGslx680(const unsigned long now) {
  if (now < touchReadAt) return;
  touchReadAt = now + TOUCH_SAMPLE_DELAY_MS;


  uint8_t frame[24] = {};
  if (!gslRead(0x80, frame, sizeof(frame))) return;

  auto finishHomeKey = [&]() {
    if (!touchHomeKeyDown) return;
    lastTouchHeldDurationMs = now - touchHomeKeyDownAt;
    if (!touchHomeKeyLongFired) touchHomeKeyTapEvent = true;
    touchHomeKeyDown = false;
    touchHomeKeyLongFired = false;
  };

  const uint8_t count = frame[0] > 5 ? 5 : frame[0];
  if (count == 0) {
    finishHomeKey();
    if (touchPressed) {
      touchPressed = false;
      touchPoint.valid = false;
      touchReleasedEvent = true;
      lastTouchHeldDurationMs = now - touchDownPoint.timestamp;
    }



    touchSnapshot.count = 0;
    touchSnapshot.reportedCount = 0;
    touchSnapshot.idsStable = false;
    updateMultiTouchGesture(touchSnapshot, now);
    return;
  }










  const auto& t = BoardConfig::ACTIVE.touch;
  bool anyKey = false;
  bool anyOnGlass = false;
  for (uint8_t i = 0; i < count; ++i) {
    const uint16_t x = (static_cast<uint16_t>(frame[7 + 4 * i]) << 8 | frame[6 + 4 * i]) & 0x0fff;
    const uint16_t y = (static_cast<uint16_t>(frame[5 + 4 * i]) << 8 | frame[4 + 4 * i]) & 0x0fff;
    if (x == 0x03a0 && y == 0x0020) anyKey = true;
  }
  uint8_t stored = 0;
  uint8_t survived = 0;
  for (uint8_t i = 0; i < count; ++i) {
    const uint16_t rawYWord = static_cast<uint16_t>(frame[5 + 4 * i]) << 8 | frame[4 + 4 * i];
    const uint16_t rawXWord = static_cast<uint16_t>(frame[7 + 4 * i]) << 8 | frame[6 + 4 * i];
    const uint16_t sampleX = rawXWord & 0x0fff;
    const uint16_t sampleY = rawYWord & 0x0fff;
    if (sampleX == 0x03a0 && sampleY == 0x0020) {
      anyKey = true;
      continue;
    }
    if (sampleX >= 920) continue;
    if (sampleX == 0 && sampleY == 0) continue;



    if (anyKey && sampleX < 64) continue;
    ++survived;
    if (stored < MAX_TOUCH_CONTACTS) {



      int16_t screenX = 0;
      int16_t screenY = 0;
      gslCalibrate(sampleX, sampleY, screenX, screenY);
      TouchPoint& point = touchSnapshot.points[stored].point;
      point.valid = true;
      point.x = mapTouchAxis(static_cast<uint16_t>(screenY), t.rawMinX, t.rawMaxX, t.rawMaxX - t.rawMinX);
      point.y = mapTouchAxis(static_cast<uint16_t>(551 - screenX), t.rawMinY, t.rawMaxY, t.rawMaxY - t.rawMinY);
      point.timestamp = now;





      touchSnapshot.points[stored].id = stored;
      ++stored;
    }
    anyOnGlass = true;
  }

  if (anyOnGlass) {
    touchHomeKeyDown = false;
    touchHomeKeyLongFired = false;
  } else if (anyKey && !touchPressed) {
    if (!touchHomeKeyDown) {
      touchHomeKeyEvent = true;
      touchHomeKeyDown = true;
      touchHomeKeyLongFired = false;
      touchHomeKeyDownAt = now;
    } else if (!touchHomeKeyLongFired && now - touchHomeKeyDownAt >= HOME_KEY_LONG_PRESS_MS) {
      touchHomeKeyLongEvent = true;
      touchHomeKeyLongFired = true;
    }
    return;
  } else {
    return;
  }









  touchSnapshot.reportedCount = survived;
  touchSnapshot.idsStable = false;
  touchSnapshot.count = stored;
  updateMultiTouchGesture(touchSnapshot, now);

  touchPoint = touchSnapshot.points[0].point;
  if (!touchPressed) {
    touchPressed = true;
    touchPressedEvent = true;
    touchDownPoint = touchPoint;
    touchUpPoint = touchPoint;
    touchMovedBeyondTapSlop = false;
    touchMovedBeyondTapReleaseSlop = false;
  } else {
    touchUpPoint = touchPoint;
    const int dx = static_cast<int>(touchUpPoint.x) - static_cast<int>(touchDownPoint.x);
    const int dy = static_cast<int>(touchUpPoint.y) - static_cast<int>(touchDownPoint.y);
    if (absInt(dx) > TOUCH_TAP_SLOP_PX || absInt(dy) > TOUCH_TAP_SLOP_PX) touchMovedBeyondTapSlop = true;
    if (absInt(dx) > TOUCH_TAP_RELEASE_SLOP_PX || absInt(dy) > TOUCH_TAP_RELEASE_SLOP_PX)
      touchMovedBeyondTapReleaseSlop = true;
  }
  if (touchSnapshot.reportedCount > 1) {
    touchMovedBeyondTapSlop = true;
    touchMovedBeyondTapReleaseSlop = true;
  }
}
#endif









#if FREEINK_TOUCH_TMA525C
namespace {
constexpr uint16_t PICCO_TOUCH_W = 480;
constexpr uint16_t PICCO_TOUCH_H = 800;
constexpr uint8_t PICCO_REPORT_TOUCH = 0x01;
constexpr uint8_t PICCO_MODE_APP = 0xF7;
constexpr uint8_t PICCO_MODE_BOOTLOADER = 0xFF;
constexpr unsigned long PICCO_TOUCH_STALE_MS = 3000;
#ifndef PICCO_TOUCH_TRACE
#define PICCO_TOUCH_TRACE 0
#endif


uint16_t piccoReadReport(uint8_t addr, uint8_t* buf, uint16_t cap) {
  if (Wire.requestFrom(addr, static_cast<uint8_t>(2), static_cast<uint8_t>(true)) != 2) return 0;
  const uint8_t low = Wire.read();
  const uint16_t len = low | (static_cast<uint16_t>(Wire.read()) << 8);
  if (len < 2 || len > cap) return 0;
  const uint8_t got = Wire.requestFrom(addr, static_cast<uint8_t>(len), static_cast<uint8_t>(true));
  for (uint8_t i = 0; i < got; ++i) buf[i] = Wire.read();
  return got == len ? len : 0;
}

bool piccoWaitInt(int8_t irq, unsigned long timeoutMs) {
  const unsigned long start = millis();
  while (irq >= 0 && digitalRead(irq) != LOW) {
    if (millis() - start > timeoutMs) return false;
    delay(1);
  }
  return true;
}


uint16_t piccoCrc(const uint8_t* p, size_t n) {
  uint16_t crc = 0xFFFF;
  while (n--) {
    crc ^= static_cast<uint16_t>(*p++) << 8;
    for (uint8_t b = 0; b < 8; ++b) crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
  }
  return crc;
}
}

void InputManager::beginTma525c() {
  piccoMaxContacts = 1;
  touchReadAt = 0;
  piccoLastPollAt = 0;
  piccoLastReportAt = 0;
  piccoLastNoReportLogAt = 0;
  piccoReportSequence = 0;
  const auto& t = BoardConfig::ACTIVE.touch;
  if (t.sda < 0 || t.scl < 0 || t.i2cAddress == 0) return;

  if (t.irq >= 0) pinMode(t.irq, INPUT_PULLUP);

  Wire.begin(t.sda, t.scl, 400000);
  Wire.setTimeOut(10);


  if (t.reset >= 0) {
    pinMode(t.reset, OUTPUT);
    digitalWrite(t.reset, HIGH);
    delay(1);
    digitalWrite(t.reset, LOW);
    delay(10);
    digitalWrite(t.reset, HIGH);
    delay(50);
  }

  uint8_t buf[128];

  for (int i = 0; i < 4 && t.irq >= 0 && digitalRead(t.irq) == LOW; ++i) {
    delay(100);
    piccoReadReport(t.i2cAddress, buf, sizeof(buf));
  }


  uint8_t mode = 0;
  for (int attempt = 0; attempt < 3 && mode == 0; ++attempt) {
    Wire.beginTransmission(t.i2cAddress);
    Wire.write(0x01);
    Wire.write(0x00);
    if (Wire.endTransmission() != 0) {
      delay(120);
      continue;
    }
    if (piccoWaitInt(t.irq, 500) && piccoReadReport(t.i2cAddress, buf, sizeof(buf)) >= 3) mode = buf[2];
  }

  if (mode == PICCO_MODE_BOOTLOADER) {


    uint8_t pkt[] = {0x04, 0x00, 0x0B, 0x00, 0x40, 0x00, 0x01, 0x3B, 0x00, 0x00, 0x00, 0x00, 0x17};
    const uint16_t crc = piccoCrc(&pkt[6], 4);
    pkt[10] = crc & 0xFF;
    pkt[11] = crc >> 8;
    Wire.beginTransmission(t.i2cAddress);
    Wire.write(pkt, sizeof(pkt));
    Wire.endTransmission();
    if (piccoWaitInt(t.irq, 500)) piccoReadReport(t.i2cAddress, buf, sizeof(buf));
    mode = PICCO_MODE_APP;
  }
  touchDataEnabled = (mode == PICCO_MODE_APP);
  LOG_INF("PICCO-INPUT", "TMA525C init mode=0x%02X enabled=%d irq=%d addr=0x%02X", mode,
          touchDataEnabled ? 1 : 0, t.irq, t.i2cAddress);
  if (touchDataEnabled) {



    static constexpr uint8_t sysinfo[] = {0x04, 0x00, 0x05, 0x00, 0x2F, 0x00, 0x02};
    bool capabilityConfirmed = false;
    Wire.beginTransmission(t.i2cAddress);
    Wire.write(sysinfo, sizeof(sysinfo));
    if (Wire.endTransmission() == 0) {
      const unsigned long startedAt = millis();
      for (uint8_t attempt = 0; attempt < 8; ++attempt) {
        const unsigned long elapsed = millis() - startedAt;
        if (elapsed >= 500 || !piccoWaitInt(t.irq, 500 - elapsed)) break;
        const uint16_t len = piccoReadReport(t.i2cAddress, buf, sizeof(buf));
        if (len >= 51 && buf[2] == 0x1F && (buf[4] & 0x7F) == 0x02 && buf[50] > 0 && buf[50] <= 31) {
          piccoMaxContacts = buf[50];
          capabilityConfirmed = true;
          break;
        }
      }
    }
    if (!capabilityConfirmed)
      LOG_ERR("INPUT", "Picco multi-touch capability query failed; keeping single touch");
    else
      LOG_INF("INPUT", "Picco touch reports up to %u contacts", piccoMaxContacts);
  }
#ifdef TOUCH_PROBE_DEBUG
  touchDebugPrintf("[touch] Picco TMA525C mode=0x%02X enabled=%d\r\n", mode, touchDataEnabled);
#endif
}

void InputManager::pollTma525c(const unsigned long now) {
  if (!touchDataEnabled) return;
  const auto& t = BoardConfig::ACTIVE.touch;

  const int irqLevel = t.irq >= 0 ? digitalRead(t.irq) : -1;
  if (t.irq >= 0 && irqLevel != LOW) {
    if (touchPressed && now - touchPoint.timestamp >= PICCO_TOUCH_STALE_MS) {
      cancelMultiTouchGesture();
      touchSnapshot = {};
      touchSnapshot.idsStable = true;
      touchReleasedEvent = true;
      lastTouchHeldDurationMs = now - touchDownPoint.timestamp;
      touchUpPoint = touchPoint;
      touchPressed = false;
      touchPoint.valid = false;
    }
    return;
  }
  const unsigned long pollDelta = piccoLastPollAt == 0 ? 0 : now - piccoLastPollAt;
  piccoLastPollAt = now;
  (void)irqLevel;
  (void)pollDelta;

  uint8_t buf[128];
  const uint16_t len = piccoReadReport(t.i2cAddress, buf, sizeof(buf));
  if (len < 7 || buf[2] != PICCO_REPORT_TOUCH) {
    if (touchPressed && now - piccoLastNoReportLogAt >= 100) {
#if PICCO_TOUCH_TRACE
      LOG_INF("PICCO-INPUT", "poll seq=%lu dt=%lums irq=%d reportLen=%u no-report pressed=1 held=%lums",
              static_cast<unsigned long>(piccoReportSequence), pollDelta, irqLevel, len,
              now - touchDownPoint.timestamp);
#endif
      piccoLastNoReportLogAt = now;
    }
    if (touchPressed && now - touchPoint.timestamp >= PICCO_TOUCH_STALE_MS) {
      LOG_INF("PICCO-INPUT", "RELEASE reason=stale seq=%lu held=%lums lastReportAge=%lums",
              static_cast<unsigned long>(piccoReportSequence), now - touchDownPoint.timestamp,
              now - piccoLastReportAt);
      cancelMultiTouchGesture();
      touchSnapshot = {};
      touchSnapshot.idsStable = true;
      touchReleasedEvent = true;
      lastTouchHeldDurationMs = now - touchDownPoint.timestamp;
      touchUpPoint = touchPoint;
      touchPressed = false;
      touchPoint.valid = false;
    }
    return;
  }
  const uint8_t count = buf[5] & 0x1F;
  const unsigned long reportDelta = piccoLastReportAt == 0 ? 0 : now - piccoLastReportAt;
  piccoLastReportAt = now;
  ++piccoReportSequence;
  (void)reportDelta;
  constexpr uint8_t headerBytes = 7;
  constexpr uint8_t recordBytes = 10;
  if (count > MAX_TOUCH_CONTACTS || len < headerBytes + count * recordBytes) {


    blockMultiTouchGesture();
    touchSnapshot = {};
    touchSnapshot.idsStable = true;
    touchSnapshot.reportedCount = count;
    LOG_ERR("PICCO-INPUT", "INVALID seq=%lu dt=%lums irq=%d contacts=%u bytes=%u pressed=%d",
            static_cast<unsigned long>(piccoReportSequence), reportDelta, irqLevel, count, len,
            touchPressed ? 1 : 0);
    return;
  }


  TouchSnapshot next{};
  next.idsStable = true;
  for (uint8_t i = 0; i < count; ++i) {
    const uint8_t* rec = &buf[headerBytes + i * recordBytes];
    const uint8_t event = (rec[1] >> 5) & 0x03;
    if (event == 3) continue;
    const uint8_t id = rec[1] & 0x1F;
    const uint16_t x = static_cast<uint16_t>(rec[2]) | (static_cast<uint16_t>(rec[3]) << 8);
    const uint16_t y = static_cast<uint16_t>(rec[4]) | (static_cast<uint16_t>(rec[5]) << 8);
    bool duplicate = false;
    for (uint8_t j = 0; j < next.count; ++j) duplicate |= next.points[j].id == id;
    if (x > PICCO_TOUCH_W || y > PICCO_TOUCH_H || duplicate) {
      blockMultiTouchGesture();
      touchSnapshot = {};
      touchSnapshot.idsStable = true;
      touchSnapshot.reportedCount = count;
      LOG_ERR("PICCO-INPUT", "INVALID seq=%lu dt=%lums irq=%d id=%u x=%u y=%u pressed=%d",
              static_cast<unsigned long>(piccoReportSequence), reportDelta, irqLevel, id, x, y,
              touchPressed ? 1 : 0);
      return;
    }
    const uint16_t px = PICCO_TOUCH_W - x;
    const uint16_t py = PICCO_TOUCH_H - y;
    const uint16_t sx = t.swapXY ? py : px;
    const uint16_t sy = t.swapXY ? px : py;
    auto& contact = next.points[next.count++];
    contact.id = id;
    contact.point.valid = true;
    contact.point.x = mapTouchAxis(sx, t.rawMinX, t.rawMaxX, t.rawMaxX - t.rawMinX);
    contact.point.y = mapTouchAxis(sy, t.rawMinY, t.rawMaxY, t.rawMaxY - t.rawMinY);
    if (t.flipX) contact.point.x = static_cast<uint16_t>((t.rawMaxX - t.rawMinX) - contact.point.x);
    if (t.flipY) contact.point.y = static_cast<uint16_t>((t.rawMaxY - t.rawMinY) - contact.point.y);
    contact.point.timestamp = now;
  }
  next.reportedCount = next.count;


  if (touchPressed && touchSnapshot.count == 1 && next.count == 1 && touchSnapshot.points[0].id != next.points[0].id) {
    blockMultiTouchGesture();
  }
  touchSnapshot = next;
  if (piccoMaxContacts >= 2)
    updateMultiTouchGesture(touchSnapshot, now);
  else if (next.count > 1)
    blockMultiTouchGesture();

  if (next.count == 0) {
#if PICCO_TOUCH_TRACE
    LOG_INF("PICCO-INPUT", "REPORT seq=%lu dt=%lums irq=%d len=%u contacts=0 lift=1 pressedBefore=%d",
            static_cast<unsigned long>(piccoReportSequence), reportDelta, irqLevel, len,
            touchPressed ? 1 : 0);
#endif
    if (touchPressed) {
      LOG_INF("PICCO-INPUT", "RELEASE reason=lift seq=%lu held=%lums", static_cast<unsigned long>(piccoReportSequence),
              now - touchDownPoint.timestamp);
      touchReleasedEvent = true;
      lastTouchHeldDurationMs = now - touchDownPoint.timestamp;
      touchUpPoint = touchPoint;
    }
    touchPressed = false;
    touchPoint.valid = false;
    return;
  }

  touchPoint = next.points[0].point;
  const uint8_t primaryId = next.points[0].id;
  const uint8_t primaryEvent = (buf[headerBytes + 1] >> 5) & 0x03;
  (void)primaryId;
  (void)primaryEvent;
#if PICCO_TOUCH_TRACE
  LOG_INF("PICCO-INPUT", "REPORT seq=%lu dt=%lums pollDt=%lums irq=%d len=%u contacts=%u id=%u event=%u raw=%u,%u mapped=%u,%u pressedBefore=%d",
          static_cast<unsigned long>(piccoReportSequence), reportDelta, pollDelta, irqLevel, len, next.count,
          primaryId, primaryEvent,
          static_cast<unsigned>(buf[headerBytes + 2]) | (static_cast<unsigned>(buf[headerBytes + 3]) << 8),
          static_cast<unsigned>(buf[headerBytes + 4]) | (static_cast<unsigned>(buf[headerBytes + 5]) << 8),
          touchPoint.x, touchPoint.y, touchPressed ? 1 : 0);
#endif
  if (!touchPressed) {
    touchPressedEvent = true;
    touchDownPoint = touchPoint;
    LOG_INF("PICCO-INPUT", "PRESS seq=%lu native=%u,%u", static_cast<unsigned long>(piccoReportSequence),
            touchPoint.x, touchPoint.y);
    touchMovedBeyondTapSlop = false;
    touchMovedBeyondTapReleaseSlop = false;
  }
  touchUpPoint = touchPoint;
  const int dx = static_cast<int>(touchUpPoint.x) - static_cast<int>(touchDownPoint.x);
  const int dy = static_cast<int>(touchUpPoint.y) - static_cast<int>(touchDownPoint.y);
  if (absInt(dx) > TOUCH_TAP_SLOP_PX || absInt(dy) > TOUCH_TAP_SLOP_PX) touchMovedBeyondTapSlop = true;
  if (absInt(dx) > TOUCH_TAP_RELEASE_SLOP_PX || absInt(dy) > TOUCH_TAP_RELEASE_SLOP_PX)
    touchMovedBeyondTapReleaseSlop = true;
  if (next.count > 1) {
    touchMovedBeyondTapSlop = true;
    touchMovedBeyondTapReleaseSlop = true;
  }
  touchPressed = true;
  touchReleaseAt = now + PICCO_TOUCH_STALE_MS;
}
#endif



#if FREEINK_TOUCH_GT911
void InputManager::beginGt911() {
  const auto& t = BoardConfig::ACTIVE.touch;







  if (t.powerEnable >= 0) {
    gpio_hold_dis(static_cast<gpio_num_t>(t.powerEnable));
    pinMode(t.powerEnable, OUTPUT);


    digitalWrite(t.powerEnable, t.powerEnableActiveHigh ? HIGH : LOW);
    delay(50);
  }

  if (t.sda >= 0 && t.scl >= 0) {
    Wire.begin(t.sda, t.scl, 400000);
    Wire.setTimeOut(10);
  }

  auto resetWithIntLevel = [&](const uint8_t level) {
    if (t.reset < 0 || t.irq < 0) return;
    pinMode(t.irq, OUTPUT);
    pinMode(t.reset, OUTPUT);
    digitalWrite(t.reset, LOW);
    digitalWrite(t.irq, level);
    delay(10);
    digitalWrite(t.reset, HIGH);
    delay(10);
    digitalWrite(t.irq, level);
    delay(50);
    pinMode(t.irq, INPUT);
    delay(50);
  };

  auto probeCandidates = [&]() {
    const uint8_t candidates[2] = {t.i2cAddress, t.i2cAddressAlt};
    for (uint8_t a : candidates) {
      if (a == 0) continue;
      Wire.beginTransmission(a);
      if (Wire.endTransmission() == 0) {
        gt911Addr = a;
        return true;
      }
    }
    return false;
  };





  gt911Addr = 0;
  resetWithIntLevel(LOW);
  if (!probeCandidates()) {
    resetWithIntLevel(HIGH);
    probeCandidates();
  }

  touchDataEnabled = (gt911Addr != 0);
#if FREEINK_GT911_INT_WAKE



  touchWakeIrqUsable = false;
  gt911StatusFault = false;
  if (touchDataEnabled) {
    gt911ConfigureIntWake();
  }
#endif
#ifdef TOUCH_PROBE_DEBUG
  touchDebugPrintf(
      "[touch] GT911 probe: addr=0x%02X enabled=%d (sda=%d scl=%d "
      "cand=0x%02X/0x%02X)\n",
      gt911Addr, touchDataEnabled, t.sda, t.scl, t.i2cAddress, t.i2cAddressAlt);
#endif
}

bool InputManager::gt911ReadReg(const uint16_t reg, uint8_t* buf, const uint8_t len) {
  Wire.beginTransmission(gt911Addr);
  Wire.write(static_cast<uint8_t>(reg >> 8));
  Wire.write(static_cast<uint8_t>(reg & 0xFF));
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  const uint8_t got = Wire.requestFrom(gt911Addr, len, static_cast<uint8_t>(true));
  if (got != len) {
    while (Wire.available()) Wire.read();
    return false;
  }
  for (uint8_t i = 0; i < len; ++i) {
    buf[i] = Wire.read();
  }
  return true;
}

bool InputManager::gt911ClearStatus() {
  Wire.beginTransmission(gt911Addr);
  Wire.write(0x81);
  Wire.write(0x4E);
  Wire.write(static_cast<uint8_t>(0x00));
  return Wire.endTransmission() == 0;
}

#if FREEINK_GT911_INT_WAKE







namespace {
constexpr uint16_t GT911_CONFIG_START = 0x8047;
constexpr uint16_t GT911_CONFIG_LEN = 184;
constexpr uint8_t GT911_CONFIG_CHUNK = 64;
constexpr uint16_t GT911_MODULE_SWITCH1_OFF = 0x804D - GT911_CONFIG_START;
constexpr uint8_t GT911_INT_MODE_MASK = 0x03;
constexpr uint8_t GT911_INT_LOW_LEVEL = 0x02;
constexpr uint16_t GT911_CONFIG_FRESH = 0x8100;


constexpr uint8_t GT911_FRESH_POLL_MS = 10;
constexpr uint8_t GT911_FRESH_POLL_TRIES = 10;




constexpr uint8_t GT911_STATUS_TRIES = 3;
constexpr uint8_t GT911_STATUS_RETRY_MS = 2;
}

bool InputManager::gt911WriteReg(const uint16_t reg, const uint8_t* buf, const uint8_t len) {
  Wire.beginTransmission(gt911Addr);
  Wire.write(static_cast<uint8_t>(reg >> 8));
  Wire.write(static_cast<uint8_t>(reg & 0xFF));
  for (uint8_t i = 0; i < len; ++i) {
    Wire.write(buf[i]);
  }
  return Wire.endTransmission() == 0;
}

void InputManager::gt911ConfigureIntWake() {
  const auto& t = BoardConfig::ACTIVE.touch;
  if (gt911Addr == 0 || t.irq < 0) {
    return;
  }
  if (!t.irqActiveLow) {





    LOG_ERR("INPUT", "GT911 INT wake: profile does not declare an active-low INT; touch stays polled");
    return;
  }

  uint8_t cfg[GT911_CONFIG_LEN + 1] = {};
  for (uint16_t off = 0; off < sizeof(cfg); off += GT911_CONFIG_CHUNK) {
    const uint16_t remaining = static_cast<uint16_t>(sizeof(cfg) - off);
    const uint8_t len = static_cast<uint8_t>(remaining < GT911_CONFIG_CHUNK ? remaining : GT911_CONFIG_CHUNK);
    if (!gt911ReadReg(static_cast<uint16_t>(GT911_CONFIG_START + off), cfg + off, len)) {
      LOG_ERR("INPUT", "GT911 INT wake: config read failed; not configured, touch stays polled");
      return;
    }
  }

  uint8_t sum = 0;
  for (uint16_t i = 0; i < GT911_CONFIG_LEN; ++i) {
    sum = static_cast<uint8_t>(sum + cfg[i]);
  }
  if (static_cast<uint8_t>(sum + cfg[GT911_CONFIG_LEN]) != 0) {


    LOG_ERR("INPUT", "GT911 INT wake: config checksum mismatch; not configured, touch stays polled");
    return;
  }





  const uint8_t switch1 = cfg[GT911_MODULE_SWITCH1_OFF];
  const bool needsPatch = (switch1 & GT911_INT_MODE_MASK) != GT911_INT_LOW_LEVEL;
  if (needsPatch) {
    const uint8_t patched = static_cast<uint8_t>((switch1 & ~GT911_INT_MODE_MASK) | GT911_INT_LOW_LEVEL);








    sum = static_cast<uint8_t>(sum - cfg[0] - switch1 + patched);
    const uint8_t version = 0x00;
    const uint8_t tail[2] = {static_cast<uint8_t>(-sum), 0x01};
    if (!gt911WriteReg(GT911_CONFIG_START, &version, 1) || !gt911WriteReg(0x804D, &patched, 1) ||
        !gt911WriteReg(0x80FF, tail, sizeof(tail))) {
      LOG_ERR("INPUT", "GT911 INT wake: config write failed; touch stays polled");
      return;
    }







    bool consumed = false;
    for (uint8_t tries = 0; !consumed && tries < GT911_FRESH_POLL_TRIES; ++tries) {
      delay(GT911_FRESH_POLL_MS);
      uint8_t fresh = 1;
      consumed = gt911ReadReg(GT911_CONFIG_FRESH, &fresh, 1) && fresh == 0;
    }
    if (!consumed) {
      LOG_ERR("INPUT", "GT911 INT wake: Config_Fresh not cleared (table not consumed); touch stays polled");
      return;
    }
  }



  uint8_t readback = 0;
  if (!gt911ReadReg(0x804D, &readback, 1)) {
    LOG_ERR("INPUT", "GT911 INT wake: mode readback failed; touch stays polled");
    return;
  }
  if ((readback & GT911_INT_MODE_MASK) != GT911_INT_LOW_LEVEL) {
    LOG_ERR("INPUT", "GT911 INT wake: mode readback 0x%02X is not low level; touch stays polled", readback);
    return;
  }




  pinMode(t.irq, INPUT_PULLUP);






  touchWakeIrqUsable = true;
  gt911ClearStatusForWake();
  LOG_INF("INPUT", "GT911 INT wake: INT=%d low-level hold (%s)", t.irq,
          needsPatch ? "patched this boot" : "already set");
}




uint8_t InputManager::gt911StatusTries() const {
  return touchWakeIrqUsable && !gt911StatusFault ? GT911_STATUS_TRIES : 1;
}

bool InputManager::gt911ReadStatusForWake(uint8_t& status) {
  const uint8_t tries = gt911StatusTries();
  for (uint8_t i = 0; i < tries; ++i) {
    if (i > 0) delay(GT911_STATUS_RETRY_MS);
    if (gt911ReadReg(0x814E, &status, 1)) return true;
  }
  gt911SetStatusFault(true);
  return false;
}

void InputManager::gt911ClearStatusForWake() {
  const uint8_t tries = gt911StatusTries();
  for (uint8_t i = 0; i < tries; ++i) {
    if (i > 0) delay(GT911_STATUS_RETRY_MS);
    if (gt911ClearStatus()) {
      gt911SetStatusFault(false);
      return;
    }
  }
  gt911SetStatusFault(true);
}




void InputManager::gt911SetStatusFault(const bool fault) {

  if (fault == gt911StatusFault || (fault && !touchWakeIrqUsable)) return;
  gt911StatusFault = fault;
  if (fault) {
    LOG_ERR("INPUT", "GT911 INT wake: status register 0x814E unreachable on I2C; INT withdrawn as a wake source");
  } else {
    LOG_INF("INPUT", "GT911 INT wake: status register reachable again; INT usable as a wake source");
  }
}

#endif

#endif

#if FREEINK_TOUCH_FT6336U
#if FREEINK_DEVICE_MURPHY_M4



namespace {
i2c_master_bus_handle_t gM4TouchBus = nullptr;
i2c_master_dev_handle_t gM4TouchDevice = nullptr;

static bool m4NativeWriteReg(const uint8_t reg, const uint8_t value) {
  if (gM4TouchDevice == nullptr) return false;
  const uint8_t payload[] = {reg, value};
  const esp_err_t err = i2c_master_transmit(gM4TouchDevice, payload, sizeof(payload), 20);
  if (err != ESP_OK) {
    esp_rom_printf("[touch] M4 write reg=%02X value=%02X failed: %s (%d)\r\n", reg, value, esp_err_to_name(err),
                   static_cast<int>(err));
  }
  return err == ESP_OK;
}

static bool m4NativeRead(const uint8_t reg, uint8_t* buf, const size_t len) {
  if (gM4TouchDevice == nullptr) return false;
  const esp_err_t err = i2c_master_transmit_receive(gM4TouchDevice, &reg, 1, buf, len, 20);
  if (err != ESP_OK) {
    esp_rom_printf("[touch] M4 read reg=%02X len=%u failed: %s (%d)\r\n", reg, static_cast<unsigned>(len),
                   esp_err_to_name(err), static_cast<int>(err));
  }
  return err == ESP_OK;
}

static bool m4NativeBegin(const int sda, const int scl, const uint8_t address) {
  if (gM4TouchDevice != nullptr) return true;
  i2c_master_bus_config_t busConfig = {};
  busConfig.i2c_port = I2C_NUM_1;
  busConfig.sda_io_num = static_cast<gpio_num_t>(sda);
  busConfig.scl_io_num = static_cast<gpio_num_t>(scl);
  busConfig.clk_source = I2C_CLK_SRC_DEFAULT;
  busConfig.glitch_ignore_cnt = 7;
  busConfig.flags.enable_internal_pullup = 1;
  esp_err_t err = i2c_new_master_bus(&busConfig, &gM4TouchBus);
  if (err != ESP_OK) {
    esp_rom_printf("[touch] M4 i2c_new_master_bus failed: %s\r\n", esp_err_to_name(err));
    return false;
  }
  i2c_device_config_t deviceConfig = {};
  deviceConfig.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  deviceConfig.device_address = address;
  deviceConfig.scl_speed_hz = 100000;
  err = i2c_master_bus_add_device(gM4TouchBus, &deviceConfig, &gM4TouchDevice);
  if (err != ESP_OK) {
    esp_rom_printf("[touch] M4 i2c_master_bus_add_device failed: %s\r\n", esp_err_to_name(err));
    i2c_del_master_bus(gM4TouchBus);
    gM4TouchBus = nullptr;
    return false;
  }
  return true;
}

}
#endif

void InputManager::beginFt6336u() {
  const auto& t = BoardConfig::ACTIVE.touch;

  if (t.powerEnable >= 0) {
    gpio_hold_dis(static_cast<gpio_num_t>(t.powerEnable));
    pinMode(t.powerEnable, OUTPUT);
#if FREEINK_DEVICE_MURPHY_M4


    digitalWrite(t.powerEnable, t.powerEnableActiveHigh ? HIGH : LOW);
    delay(500);
#else
    digitalWrite(t.powerEnable, t.powerEnableActiveHigh ? HIGH : LOW);
    delay(300);
#endif
  }

  if (t.sda >= 0 && t.scl >= 0) {
#if !FREEINK_DEVICE_MURPHY_M4
    Wire.begin(t.sda, t.scl, 400000);
    Wire.setTimeOut(10);
#endif
  }

#if FREEINK_DEVICE_MURPHY_M4
  if (t.sda >= 0 && t.scl >= 0) {


    if (t.reset >= 0) {
      pinMode(t.reset, OUTPUT);
      digitalWrite(t.reset, LOW);
      delay(50);
      digitalWrite(t.reset, HIGH);
      delay(100);
    }
    if (t.irq >= 0) {
      pinMode(t.irq, INPUT_PULLUP);
    }
    const bool busOk = m4NativeBegin(t.sda, t.scl, t.i2cAddress);
    const bool initModeOk = busOk && m4NativeWriteReg(0x00, 0x00);
    const bool initThresholdOk = busOk && m4NativeWriteReg(0x80, 0x16);
    const bool initRateOk = busOk && m4NativeWriteReg(0x88, 0x04);
    esp_rom_printf("[touch] M4 OEM volatile init: mode=%d threshold=%d rate=%d\r\n", static_cast<int>(initModeOk),
                   static_cast<int>(initThresholdOk), static_cast<int>(initRateOk));

    uint8_t probeFrame[11] = {};
    touchDataEnabled = busOk && m4NativeRead(0x02, probeFrame, sizeof(probeFrame));
    esp_rom_printf("[touch] M4 FT6336U ready=%d (SDA=%d SCL=%d address=0x%02X)\r\n", static_cast<int>(touchDataEnabled),
                   t.sda, t.scl, t.i2cAddress);
  }
#else

  for (int attempt = 0; attempt < 3 && !touchDataEnabled; ++attempt) {
    if (attempt > 0) delay(100);
    Wire.beginTransmission(t.i2cAddress);
    touchDataEnabled = (Wire.endTransmission() == 0);
  }
#endif
}








void InputManager::pollFt6336u(const unsigned long now) {
  if (!touchDataEnabled) return;

#if FREEINK_DEVICE_MURPHY_M4


  const int irq = BoardConfig::ACTIVE.touch.irq;
  if (irq >= 0 && digitalRead(irq) != LOW && !touchPressed) return;
#endif

  if (now < touchReadAt) return;
  touchReadAt = now + TOUCH_SAMPLE_DELAY_MS;

  uint8_t data[16] = {};
  bool gotData = false;
#if FREEINK_DEVICE_MURPHY_M4
  uint8_t nativeFrame[11] = {};
  gotData = m4NativeRead(0x02, nativeFrame, sizeof(nativeFrame));
  if (gotData) {

    data[2] = nativeFrame[0];
    data[3] = nativeFrame[1];
    data[4] = nativeFrame[2];
    data[5] = nativeFrame[3];
    data[6] = nativeFrame[4];
  }
#else
  const uint8_t addr = BoardConfig::ACTIVE.touch.i2cAddress;
  Wire.beginTransmission(addr);
  Wire.write(static_cast<uint8_t>(0x00));
  if (Wire.endTransmission(false) == 0) {
    const uint8_t got = Wire.requestFrom(addr, static_cast<uint8_t>(7), static_cast<uint8_t>(true));
    if (got == 7) {
      for (uint8_t i = 0; i < 7; ++i) data[i] = Wire.read();
      gotData = true;
    } else {
      while (Wire.available()) Wire.read();
    }
  }
#endif

#ifdef TOUCH_PROBE_DEBUG
  if (gotData) {
    const int intPin = BoardConfig::ACTIVE.touch.irq;
    const int intState = (intPin >= 0) ? digitalRead(intPin) : -1;
    touchDebugPrintf("[touch] FT6336U poll ok int=%d mode=%02X gest=%02X cnt=%d ev=%d raw=[%02X %02X %02X %02X]\r\n",
                     intState, data[0], data[1], data[2] & 0x0F, (data[3] >> 6) & 0x03, data[3], data[4], data[5],
                     data[6]);
  } else {
    static uint32_t failCount = 0;
    if (++failCount % 200 == 1) {
      touchDebugPrintf("[touch] FT6336U poll fail (total=%lu)\r\n", static_cast<unsigned long>(failCount));
    }
  }
#endif
  if (!gotData) return;




  const bool uniformGarbage =
      (data[3] == data[4] && data[3] == data[5] && data[3] == data[6]) || (data[4] == data[5] && data[4] == data[6]);
  const int oneCount = (data[3] == 0x01) + (data[4] == 0x01) + (data[5] == 0x01) + (data[6] == 0x01);
  const bool stuckOneGarbage = data[0] == 0x01 && data[1] == 0x01 && (data[2] & 0x0F) == 0x01 && oneCount >= 3;
  if (uniformGarbage || stuckOneGarbage) {

    if (touchPressed && BoardConfig::ACTIVE.touch.irq >= 0 && digitalRead(BoardConfig::ACTIVE.touch.irq) != 0) {
      touchPressed = false;
      touchPoint.valid = false;
    }
    return;
  }

  const uint8_t numPoints = data[2] & 0x0F;
  const uint8_t eventFlag = (data[3] >> 6) & 0x03;
  const bool isTouching = (numPoints > 0) && (eventFlag != 1);

  if (isTouching) {
    const uint16_t rawX = (static_cast<uint16_t>(data[3] & 0x0F) << 8) | data[4];
    const uint16_t rawY = (static_cast<uint16_t>(data[5] & 0x0F) << 8) | data[6];
    const auto& t = BoardConfig::ACTIVE.touch;
    const uint16_t sx = t.swapXY ? rawY : rawX;
    const uint16_t sy = t.swapXY ? rawX : rawY;



    if (sx > static_cast<uint16_t>(t.rawMaxX) || sy > static_cast<uint16_t>(t.rawMaxY)) return;
    touchPoint.valid = true;
    touchPoint.x = mapTouchAxis(sx, t.rawMinX, t.rawMaxX, t.rawMaxX - t.rawMinX);
    touchPoint.y = mapTouchAxis(sy, t.rawMinY, t.rawMaxY, t.rawMaxY - t.rawMinY);
    if (t.flipX) touchPoint.x = static_cast<uint16_t>((t.rawMaxX - t.rawMinX) - touchPoint.x);
    if (t.flipY) touchPoint.y = static_cast<uint16_t>((t.rawMaxY - t.rawMinY) - touchPoint.y);
    touchPoint.timestamp = now;
    if (!touchPressed) {
      touchPressedEvent = true;
      touchDownPoint = touchPoint;
      touchMovedBeyondTapSlop = false;
    }
    touchUpPoint = touchPoint;
    const int dx = static_cast<int>(touchUpPoint.x) - static_cast<int>(touchDownPoint.x);
    const int dy = static_cast<int>(touchUpPoint.y) - static_cast<int>(touchDownPoint.y);
    if (absInt(dx) > TOUCH_TAP_SLOP_PX || absInt(dy) > TOUCH_TAP_SLOP_PX) {
      touchMovedBeyondTapSlop = true;
    }
    touchPressed = true;
  } else {
    if (touchPressed) {
      touchReleasedEvent = true;
      lastTouchHeldDurationMs = now - touchDownPoint.timestamp;
      touchUpPoint = touchPoint;
    }
    touchPressed = false;
    touchPoint.valid = false;
  }
}
#endif

#if FREEINK_TOUCH_GT911
void InputManager::pollGt911(const unsigned long now) {
  if (gt911Addr == 0) {
    return;
  }
  uint8_t status = 0;
#if FREEINK_GT911_INT_WAKE
  const bool statusRead = gt911ReadStatusForWake(status);
#else
  const bool statusRead = gt911ReadReg(0x814E, &status, 1);
#endif
  if (!statusRead) {





    return;
  }






  if (touchHomeKeyDown && !touchHomeKeyLongFired && now - touchHomeKeyDownAt >= HOME_KEY_LONG_PRESS_MS) {
    touchHomeKeyLongEvent = true;
    touchHomeKeyLongFired = true;
  }

  if (!(status & 0x80)) {
#if FREEINK_GT911_INT_WAKE





    if (status != 0) {
      gt911ClearStatusForWake();
    } else {
      gt911SetStatusFault(false);
    }
#endif
    return;
  }



  const bool homeKeyDown = (status & 0x10) != 0;
  if (homeKeyDown && !touchHomeKeyDown) {
    touchHomeKeyEvent = true;
    touchHomeKeyDownAt = now;
    touchHomeKeyLongFired = false;
  } else if (!homeKeyDown && touchHomeKeyDown && !touchHomeKeyLongFired) {
    touchHomeKeyTapEvent = true;
  }
  touchHomeKeyDown = homeKeyDown;

  const uint8_t count = status & 0x0F;
  if (count > 0) {



    const uint8_t storedCount = std::min<uint8_t>(count, MAX_TOUCH_CONTACTS);
    uint8_t points[MAX_TOUCH_CONTACTS * 8] = {};
    if (gt911ReadReg(0x8150, points, storedCount * 8)) {
      const auto& t = BoardConfig::ACTIVE.touch;
      touchSnapshot.reportedCount = count;
      touchSnapshot.idsStable = !t.gt911CoordsAtByte0;
      touchSnapshot.count = storedCount;
      for (uint8_t i = 0; i < touchSnapshot.count; ++i) {
        const uint8_t* record = points + i * 8;
        touchSnapshot.points[i].id = t.gt911CoordsAtByte0 ? i : (record[0] & 0x0F);
        const uint8_t offset = t.gt911CoordsAtByte0 ? 0 : 1;
        const uint16_t rawX = static_cast<uint16_t>(record[offset]) | (static_cast<uint16_t>(record[offset + 1]) << 8);
        const uint16_t rawY =
            static_cast<uint16_t>(record[offset + 2]) | (static_cast<uint16_t>(record[offset + 3]) << 8);
        TouchPoint& point = touchSnapshot.points[i].point;
        point.valid = true;





        const uint16_t sx = t.swapXY ? rawY : rawX;
        const uint16_t sy = t.swapXY ? rawX : rawY;
        point.x = mapTouchAxis(sx, t.rawMinX, t.rawMaxX, t.rawMaxX - t.rawMinX);
        point.y = mapTouchAxis(sy, t.rawMinY, t.rawMaxY, t.rawMaxY - t.rawMinY);
        if (t.flipX) point.x = static_cast<uint16_t>((t.rawMaxX - t.rawMinX) - point.x);
        if (t.flipY) point.y = static_cast<uint16_t>((t.rawMaxY - t.rawMinY) - point.y);
        point.timestamp = now;
      }




      updateMultiTouchGesture(touchSnapshot, now);


      touchPoint = touchSnapshot.points[0].point;
      if (!touchPressed) {
        touchPressedEvent = true;
        touchDownPoint = touchPoint;

        touchMovedBeyondTapSlop = false;
        touchMovedBeyondTapReleaseSlop = false;
      }
      touchUpPoint = touchPoint;
      const int dx = static_cast<int>(touchUpPoint.x) - static_cast<int>(touchDownPoint.x);
      const int dy = static_cast<int>(touchUpPoint.y) - static_cast<int>(touchDownPoint.y);
      if (absInt(dx) > TOUCH_TAP_SLOP_PX || absInt(dy) > TOUCH_TAP_SLOP_PX) {
        touchMovedBeyondTapSlop = true;
      }
      if (absInt(dx) > TOUCH_TAP_RELEASE_SLOP_PX || absInt(dy) > TOUCH_TAP_RELEASE_SLOP_PX) {
        touchMovedBeyondTapReleaseSlop = true;
      }


      if (touchSnapshot.reportedCount > 1) {
        touchMovedBeyondTapSlop = true;
        touchMovedBeyondTapReleaseSlop = true;
      }
#ifdef TOUCH_PROBE_DEBUG
      if (!touchPressed)
        touchDebugPrintf("[touch] press contacts=%u primary=(%u,%u)\n", touchSnapshot.count, touchPoint.x,
                         touchPoint.y);
#endif
      touchPressed = true;
    } else {



    }
  } else {
    touchSnapshot.count = 0;
    touchSnapshot.reportedCount = 0;
    touchSnapshot.idsStable = !BoardConfig::ACTIVE.touch.gt911CoordsAtByte0;
    updateMultiTouchGesture(touchSnapshot, now);
    if (touchPressed) {
      touchReleasedEvent = true;
      lastTouchHeldDurationMs = now - touchDownPoint.timestamp;
      touchUpPoint = touchPoint;
    }
    touchPressed = false;
    touchPoint.valid = false;
  }

#if FREEINK_GT911_INT_WAKE
  gt911ClearStatusForWake();
#else
  gt911ClearStatus();
#endif
}
#endif

#endif
