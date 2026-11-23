#pragma once









#include <Arduino.h>
#include <BoardConfig.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <cstdint>

class InputManager {
 public:
  InputManager();
  void begin();

  bool prepareForDeepSleep();
  uint8_t getState();


  void update();


  bool isPressed(uint8_t buttonIndex) const;



  bool isPowerButtonPhysicallyPressed() const;


  bool wasPressed(uint8_t buttonIndex) const;


  bool wasAnyPressed() const;


  bool wasReleased(uint8_t buttonIndex) const;


  bool wasAnyReleased() const;







  bool isDebouncePending() const { return lastState != currentState; }


  unsigned long getHeldTime() const;


  unsigned long getPowerButtonHeldTime() const;

  static constexpr uint8_t BTN_BACK = 0;
  static constexpr uint8_t BTN_CONFIRM = 1;
  static constexpr uint8_t BTN_LEFT = 2;
  static constexpr uint8_t BTN_RIGHT = 3;
  static constexpr uint8_t BTN_UP = 4;
  static constexpr uint8_t BTN_DOWN = 5;
  static constexpr uint8_t BTN_POWER = 6;





  static constexpr int BUTTON_ADC_PIN_1 = 1;
  static constexpr int BUTTON_ADC_PIN_2 = 2;
  static constexpr int POWER_BUTTON_PIN = BoardConfig::DEFAULT_DEVICE.input.power;

  bool isPowerButtonPressed() const;

  static const char* getButtonName(uint8_t buttonIndex);



  struct TouchPoint {
    bool valid;
    uint16_t x;
    uint16_t y;
    unsigned long timestamp;
  };











  static constexpr uint8_t MAX_TOUCH_CONTACTS = 4;
  struct MultiTouchPoint {
    uint8_t id;
    TouchPoint point;
  };
  struct TouchSnapshot {
    uint8_t count;
    uint8_t reportedCount;
    bool idsStable;
    MultiTouchPoint points[MAX_TOUCH_CONTACTS];
  };


  bool hasTouch() const;



  bool supportsMultiTouch() const;



  TouchSnapshot getTouchSnapshot() const;

  TouchPoint getTouchPoint() const;

  bool isTouchPressed() const;

  bool wasTouchPressed() const;

  bool wasTouchReleased() const;



  bool wasTouchTap(float& nx, float& ny) const;


  bool wasTouchPressedAt(float& nx, float& ny) const;


  uint8_t capacitivePageButtonMask() const {
#if FREEINK_CAP_TOUCH
    return cstVirtualButtons;
#else
    return 0;
#endif
  }



  bool isTouchTapCandidate(float& nx, float& ny, unsigned long& heldMs) const;




  bool isTouchHeldAt(float& nx, float& ny) const;

  unsigned long lastTouchHeldMs() const;


  bool wasSwipe(float& nxStart, float& nyStart, float& nxEnd, float& nyEnd) const;






  bool wasMultiTouchSwipe(uint8_t& contactCount, float& nxStart, float& nyStart, float& nxEnd, float& nyEnd,
                          unsigned long& durationMs) const;





  bool wasMultiTouchRotation(float& degrees, float& nxCenter, float& nyCenter, unsigned long& durationMs) const;





  bool wasMultiTouchPinch(float& scale, float& nxCenter, float& nyCenter, unsigned long& durationMs) const;






  bool wasTouchLongPress(float& nx, float& ny) const;





  void suppressTouchContact();




  bool wasTouchActivity() const;


  bool wasHomeKeyPressed() const;



  bool wasHomeKeyTapped() const;



  bool wasHomeKeyLongPressed() const;




  bool isHomeKeyDown() const;


  bool hasToggleSwitch() const { return BoardConfig::ACTIVE.input.toggleSwitch >= 0; }


  bool isToggleSwitchOn() const { return toggleSwitchOn; }


  bool wasToggleSwitchChanged() const { return toggleSwitchChangedEvent; }













  int8_t touchWakeIrqPin() const;



  bool touchWakeIrqActiveLow() const;






  using ButtonHook = uint8_t (*)();
  static void setButtonHook(ButtonHook hook) { s_buttonHook = hook; }



  static bool hasButtonHook() { return s_buttonHook != nullptr; }




  static void setSharedConfirmPowerShortPressEmitsPower(bool enabled) {
    s_sharedConfirmPowerShortPressEmitsPower = enabled;
  }











  void beginAsync(uint8_t taskPriority = 2, uint32_t pollMs = 15, uint8_t queueLen = 32);



  bool popPress(uint8_t& button);





  bool popTouchTap(float& nx, float& ny);




  bool popSwipe(float& nxStart, float& nyStart, float& nxEnd, float& nyEnd);






  bool popMultiTouchSwipe(uint8_t& contactCount, float& nxStart, float& nyStart, float& nxEnd, float& nyEnd,
                          unsigned long& durationMs);




  bool popMultiTouchRotation(float& degrees, float& nxCenter, float& nyCenter, unsigned long& durationMs);



  bool popMultiTouchPinch(float& scale, float& nxCenter, float& nyCenter, unsigned long& durationMs);









  struct ButtonAdcSample {
    int pin;
    int raw;
    int button;
  };




  void readButtonAdc(ButtonAdcSample& group1, ButtonAdcSample& group2);

 private:
  static ButtonHook s_buttonHook;

  QueueHandle_t _asyncQueue = nullptr;
  QueueHandle_t _asyncTapQueue = nullptr;
  QueueHandle_t _asyncSwipeQueue = nullptr;
  struct QueuedMultiTouchSwipe {
    uint16_t startX;
    uint16_t startY;
    uint16_t endX;
    uint16_t endY;
    uint8_t contactCount;
    uint16_t durationMs;
  };
  QueueHandle_t _asyncMultiTouchSwipeQueue = nullptr;
  struct QueuedMultiTouchRotation {
    float degrees;
    uint16_t centerX;
    uint16_t centerY;
    uint16_t durationMs;
  };
  QueueHandle_t _asyncMultiTouchRotationQueue = nullptr;
  struct QueuedMultiTouchPinch {
    float scale;
    uint16_t centerX;
    uint16_t centerY;
    uint16_t durationMs;
  };
  QueueHandle_t _asyncMultiTouchPinchQueue = nullptr;
  TaskHandle_t _asyncTask = nullptr;
  uint32_t _asyncPollMs = 15;
  static void asyncTaskTrampoline(void* self);
  void asyncPoll();

  int getButtonFromADC(int adcValue, const int ranges[], int numButtons);
  bool isDigitalPressed(int8_t pin) const;
  uint8_t getDigitalState() const;
  void updateConfirmBackHold(unsigned long currentTime);
  void updateConfirmPowerHold(unsigned long currentTime);
  void updateDigitalTwoButton(unsigned long currentTime);
  void applyStateChange(uint8_t state, unsigned long currentTime);




  void beginTouch();
  uint8_t serviceTouch();
  void updateTouchFromIrq(unsigned long now,
                          int irqRaw);
  void pollGt911(unsigned long now);
  void beginCst816s();
  void pollCst816s(unsigned long now);
  uint8_t cstVirtualButtons = 0;
  unsigned long cstLastSample = 0;
  void beginFt5x06();
  void pollFt5x06(unsigned long now);
  bool ft5x06WriteReg(uint8_t reg, uint8_t value);
  bool ft5x06ReadReg(uint8_t reg, uint8_t* buf, uint8_t len);
  bool readChsc6xPoint(TouchPoint& point);
  bool decodeChsc6xFrame(const uint8_t* data, size_t len, TouchPoint& point) const;
  uint16_t mapTouchAxis(uint16_t raw, uint16_t rawMin, uint16_t rawMax, uint16_t outMax) const;
  void beginGt911();
  bool gt911ReadReg(uint16_t reg, uint8_t* buf, uint8_t len);
  bool gt911ClearStatus();
#if FREEINK_TOUCH_GT911 && FREEINK_GT911_INT_WAKE
  bool gt911WriteReg(uint16_t reg, const uint8_t* buf, uint8_t len);


  void gt911ConfigureIntWake();



  bool gt911ReadStatusForWake(uint8_t& status);
  void gt911ClearStatusForWake();
  void gt911SetStatusFault(bool fault);
  uint8_t gt911StatusTries() const;

  bool gt911StatusFault = false;
#endif
  void beginFt6336u();
  void pollFt6336u(unsigned long now);
  void beginGslx680();
  void gslInitBody();
  static void gslInitTask(void* arg);
  void waitForTouchInit() const;
  bool gslInitRunning = false;
  void pollGslx680(unsigned long now);
  static void gslCalibrate(uint16_t rawX, uint16_t rawY, int16_t& screenX, int16_t& screenY);
  bool gslWrite(uint8_t reg, const uint8_t* data, uint8_t len);
  bool gslRead(uint8_t reg, uint8_t* buf, uint8_t len);
  void beginTma525c();
  void pollTma525c(unsigned long now);
  bool gslUploadFirmware();

  enum class MultiTouchGestureState : uint8_t { Idle, Tracking, Blocked };
  struct TrackedTouchContact {
    uint8_t id;
    TouchPoint start;
    TouchPoint last;
  };
  void updateMultiTouchGesture(const TouchSnapshot& snapshot, unsigned long now);
  void startMultiTouchGesture(const TouchSnapshot& snapshot, unsigned long now);
  void blockMultiTouchGesture();
  void finishMultiTouchGesture(unsigned long now);
  void resetMultiTouchGesture();
  void cancelMultiTouchGesture();
  bool matchMultiTouchSnapshot(const TouchSnapshot& snapshot);
  bool expandMultiTouchGesture(const TouchSnapshot& snapshot, unsigned long now);
  bool findContactAssignment(const TouchSnapshot& snapshot, uint8_t trackedCount,
                             uint8_t assignment[MAX_TOUCH_CONTACTS]) const;
  bool isTrackedContact(const MultiTouchPoint& point) const;
  bool hasStableTranslationGeometry() const;
  bool hasEligibleRotationScale() const;
  bool isMultiTouchTranslation(unsigned long now) const;
  bool classifyMultiTouchRotation(unsigned long now);
  bool classifyMultiTouchPinch(unsigned long now);
  void normalizeTouchPoint(uint16_t x, uint16_t y, float& nx, float& ny) const;

  void updateToggleSwitch(unsigned long now);
  bool toggleSwitchOn = false;
  bool toggleSwitchRaw = false;
  bool toggleSwitchChangedEvent = false;
  unsigned long toggleSwitchRawSince = 0;

  uint8_t currentState;
  uint8_t lastState;
  uint8_t pressedEvents;
  uint8_t releasedEvents;
  unsigned long lastDebounceTime;
  unsigned long buttonPressStart;
  unsigned long buttonPressFinish;
  unsigned long powerButtonPressStart;
  unsigned long powerButtonPressFinish;
  unsigned long confirmBackPressStart;
  bool confirmBackPhysicalPressed;
  bool confirmBackLongPressActive;
  unsigned long confirmPowerPressStart;
  bool confirmPowerPhysicalPressed;
  bool confirmPowerLongPressActive;
  uint8_t twoButtonPhysicalState;
  unsigned long twoButtonPressStart;
  bool twoButtonLongPressActive;

  bool touchDataEnabled = false;
  bool touchWakeIrqUsable = false;
#if FREEINK_TOUCH_TMA525C
  uint8_t piccoMaxContacts = 1;
  unsigned long piccoLastPollAt = 0;
  unsigned long piccoLastReportAt = 0;
  unsigned long piccoLastNoReportLogAt = 0;
  uint32_t piccoReportSequence = 0;
#endif
  uint8_t gt911Addr = 0;
  unsigned long touchIrqPulseUntil = 0;
  unsigned long touchReadAt = 0;
  unsigned long touchReleaseAt = 0;
  bool touchPressed = false;
  bool touchPressedEvent = false;
  bool touchReleasedEvent = false;
  bool touchHomeKeyEvent = false;
  bool touchHomeKeyDown = false;
  bool touchHomeKeyTapEvent = false;
  bool touchHomeKeyLongEvent = false;
  bool touchHomeKeyLongFired = false;

  unsigned long touchHomeKeyDownAt = 0;
  static constexpr unsigned long HOME_KEY_LONG_PRESS_MS = 700;
  TouchPoint touchPoint = {false, 0, 0, 0};
  TouchSnapshot touchSnapshot{};
  MultiTouchGestureState multiTouchGestureState = MultiTouchGestureState::Idle;
  TrackedTouchContact multiTouchContacts[MAX_TOUCH_CONTACTS] = {};
  uint8_t trackedTouchContactCount = 0;
  bool multiTouchRotationEligible = false;
  bool touchMultiContactSequence = false;
  bool multiTouchSwipeEvent = false;
  uint8_t multiTouchSwipeContactCount = 0;
  uint16_t multiTouchSwipeStartX = 0;
  uint16_t multiTouchSwipeStartY = 0;
  uint16_t multiTouchSwipeEndX = 0;
  uint16_t multiTouchSwipeEndY = 0;
  uint16_t multiTouchSwipeDurationMs = 0;
  bool multiTouchRotationEvent = false;
  float multiTouchRotationDegrees = 0.0f;
  uint16_t multiTouchRotationCenterX = 0;
  uint16_t multiTouchRotationCenterY = 0;
  uint16_t multiTouchRotationDurationMs = 0;
  bool multiTouchPinchEvent = false;
  float multiTouchPinchScale = 1.0f;
  uint16_t multiTouchPinchCenterX = 0;
  uint16_t multiTouchPinchCenterY = 0;
  uint16_t multiTouchPinchDurationMs = 0;
  TouchPoint touchDownPoint = {false, 0, 0, 0};
  TouchPoint touchUpPoint = {false, 0, 0, 0};
  unsigned long lastTouchHeldDurationMs = 0;
  bool touchMovedBeyondTapSlop = false;
  bool touchMovedBeyondTapReleaseSlop = false;
  bool touchLongPressEvent = false;
  bool touchLongPressFired = false;
  bool touchSuppressed = false;



  static constexpr int NUM_BUTTONS_1 = 4;
  static const int ADC_RANGES_1[];

  static constexpr int NUM_BUTTONS_2 = 2;
  static const int ADC_RANGES_2[];

  static constexpr int ADC_NO_BUTTON = 3900;
  static constexpr unsigned long DEBOUNCE_DELAY = 5;
  static constexpr unsigned long CONFIRM_BACK_HOLD_MS = 650;
  static constexpr unsigned long CONFIRM_POWER_HOLD_MS = 400;
  static constexpr unsigned long TWO_BUTTON_HOLD_MS = 650;



  static constexpr unsigned long TOUCH_IRQ_PULSE_MS = 120;
  static constexpr unsigned long TOUCH_SAMPLE_DELAY_MS = 8;
  static constexpr int TOUCH_TAP_SLOP_PX = 28;
  static constexpr int TOUCH_SWIPE_MIN_PX = 60;
  static constexpr int TOUCH_TAP_RELEASE_SLOP_PX = TOUCH_SWIPE_MIN_PX - 1;
  static constexpr unsigned long TOUCH_SWIPE_MAX_MS = 700;
  static constexpr unsigned long TOUCH_MULTI_SWIPE_MAX_MS = 2000;
  static constexpr int TOUCH_MULTI_CONTACT_SEPARATION_SLOP_PX = 45;
  static constexpr int64_t TOUCH_CONTACT_ASSIGNMENT_AMBIGUITY_PX_SQ = 64;
  static constexpr unsigned long TOUCH_LONG_PRESS_MS = 500;

  static constexpr uint8_t TOUCH_READ_COMMAND = 0x00;
  static constexpr uint8_t TOUCH_FRAME_SIZE = 16;

  static const char* BUTTON_NAMES[];
  static bool s_sharedConfirmPowerShortPressEmitsPower;
};
