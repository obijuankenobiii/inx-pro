/**
 * @file ReaderButtonBindings.cpp
 * @brief Definitions for ReaderButtonBindings.
 */

#include "ReaderButtonBindings.h"

#include <Arduino.h>
#include <GfxRenderer.h>

#include "EpubActivity.h"
#include "EpubNavigation.h"
#include "state/SystemSetting.h"
#if FREEINK_DEVICE_X4PRO
#include "system/Frontlight.h"
#endif

bool ReaderButtonBindings::handleInput(EpubActivity& act) {
  const unsigned long now = millis();
  if (pendingDoubleTap_ && now - pendingDoubleTapAtMs_ > kDoubleTapWindowMs) {
    pendingDoubleTap_ = false;
    if (READER_SETTINGS.pageTurnMode == ReaderSetting::PAGE_TURN_TAP) {
      dispatch(act, pendingDoubleTapForward_ ? SystemSetting::BTN_ACTION_PAGE_NEXT
                                             : SystemSetting::BTN_ACTION_PAGE_PREVIOUS);
      return true;
    }
  }

  const bool touchEnabled = !act.settingsDrawer || act.settingsDrawer->isTouchEnabled();
  if (touchEnabled && act.mappedInput.hasTouch()) {
    float tapNx = 0.0f, tapNy = 0.0f;
    if (act.mappedInput.wasTouchTapInScreen(act.renderer, tapNx, tapNy)) {
      const bool forward = tapNx >= 0.5f;
      if (READER_SETTINGS.doubleTapAction != SystemSetting::BTN_ACTION_NONE) {
        if (pendingDoubleTap_ && now - pendingDoubleTapAtMs_ <= kDoubleTapWindowMs) {
          pendingDoubleTap_ = false;
          dispatch(act, READER_SETTINGS.doubleTapAction);
        } else {
          pendingDoubleTap_ = true;
          pendingDoubleTapForward_ = forward;
          pendingDoubleTapAtMs_ = now;
        }
        return true;
      }
      if (READER_SETTINGS.pageTurnMode == ReaderSetting::PAGE_TURN_SWIPE && !forward) {
        act.pauseReadingStats();
        act.openTableOfContents();
        return true;
      }
      if (READER_SETTINGS.pageTurnMode == ReaderSetting::PAGE_TURN_TAP) {
        dispatch(act, forward ? SystemSetting::BTN_ACTION_PAGE_NEXT : SystemSetting::BTN_ACTION_PAGE_PREVIOUS);
        return true;
      }
    }
  }

  if (handleButton(act, MappedInputManager::Button::Up, upState_, READER_SETTINGS.btnLeftAction,
                   READER_SETTINGS.btnLeftLongAction)) {
    return true;
  }
  if (handleButton(act, MappedInputManager::Button::Down, downState_, READER_SETTINGS.btnRightAction,
                   READER_SETTINGS.btnRightLongAction)) {
    return true;
  }
  return false;
}

bool ReaderButtonBindings::handleButton(EpubActivity& act, const MappedInputManager::Button button,
                                        PressState& state, const uint8_t shortAction, const uint8_t longAction) {
  const bool isPressed = act.mappedInput.isPressed(button);
  const unsigned long now = millis();

  if (isPressed && !state.active) {
    state.active = true;
    state.pressStartMs = now;
    state.longFired = false;
    return false;
  }

  if (isPressed && state.active && !state.longFired && now - state.pressStartMs >= kLongPressMs) {
    state.longFired = true;
    dispatch(act, longAction);
    return true;
  }

  if (!isPressed && state.active) {
    state.active = false;
    if (!state.longFired) {
      dispatch(act, shortAction);
      return true;
    }
    return false;
  }

  return false;
}

void ReaderButtonBindings::reset() {
  upState_ = PressState{};
  downState_ = PressState{};
  pendingDoubleTap_ = false;
  pendingDoubleTapForward_ = false;
  pendingDoubleTapAtMs_ = 0;
}

void ReaderButtonBindings::dispatch(EpubActivity& act, const uint8_t action) {
  switch (action) {
    case SystemSetting::BTN_ACTION_NONE:
      break;
    case SystemSetting::BTN_ACTION_PAGE_NEXT:
      act.endPageTimer();
      act.pageTurn(true);
      act.lastAutoPageTurnTime = millis();
      break;
    case SystemSetting::BTN_ACTION_PAGE_PREVIOUS:
      act.endPageTimer();
      act.pageTurn(false);
      act.lastAutoPageTurnTime = millis();
      break;
    case SystemSetting::BTN_ACTION_OPEN_SETTINGS:
      act.pauseReadingStats();
      act.toggleSettingsDrawer();
      break;
    case SystemSetting::BTN_ACTION_TABLE_OF_CONTENTS:
      act.openTableOfContents();
      break;
    case SystemSetting::BTN_ACTION_CHANGE_ORIENTATION:
      act.orientationPicker_.enter(act);
      break;
    case SystemSetting::BTN_ACTION_APPLY_PRESET:
      act.pauseReadingStats();
      act.presetPicker_.enter(act);
      break;
    case SystemSetting::BTN_ACTION_QUICK_ACTIONS:
      act.pauseReadingStats();
      act.quickActionsUi_.enter(act);
      break;
    case SystemSetting::BTN_ACTION_GENERATE_FULL_DATA:
      act.pauseReadingStats();
      act.navigation_->generateFullData();
      break;
    case SystemSetting::BTN_ACTION_GENERATE_THUMBNAIL:
      act.pauseReadingStats();
      act.navigation_->regenerateThumbnail();
      break;
    case SystemSetting::BTN_ACTION_GO_TO_PERCENT:
      act.pauseReadingStats();
      act.goToPercentUi_.enter(act);
      break;
    case SystemSetting::BTN_ACTION_TOGGLE_LIGHT:
#if FREEINK_DEVICE_X4PRO
      if (frontlight.present()) {
        if (frontlight.brightness() > 0) {
          frontlight.off();
        } else {
          frontlight.on();
        }
        frontlight_ui::persist();
      }
#endif
      break;
    case SystemSetting::BTN_ACTION_ANNOTATE:
      act.pauseReadingStats();
      act.annUi_.enter(act);
      break;
    case SystemSetting::BTN_ACTION_DICTIONARY:
      act.pauseReadingStats();
      act.dictUi_.enter(act);
      break;
    case SystemSetting::BTN_ACTION_PAGE_REFRESH:
      act.invalidatePreparedPage();
      act.renderer.syncWriteBufferFromActive();
      act.renderer.displayBuffer(HalDisplay::MANUAL_REFRESH);
      act.updateRequired = true;
      break;
    case SystemSetting::BTN_ACTION_CHAPTER_SKIP_NEXT:
    case SystemSetting::BTN_ACTION_CHAPTER_SKIP_PREVIOUS: {
      const bool forward = action == SystemSetting::BTN_ACTION_CHAPTER_SKIP_NEXT;
      act.endPageTimer();
      bool spineAdvanced = false;
      if (forward) {
        if (act.currentSpineIndex < act.epub->getSpineItemsCount() - 1) {
          act.currentSpineIndex++;
          act.nextPageNumber = 0;

          act.section.reset();
          spineAdvanced = true;
        }
      } else if (act.currentSpineIndex > 0) {
        act.currentSpineIndex--;
        act.nextPageNumber = 0;

        act.section.reset();
        spineAdvanced = true;
      }
      if (spineAdvanced) {
        act.startPageTimer();
        act.lastAutoPageTurnTime = millis();
        act.updateRequired = true;
      }
      break;
    }
    case SystemSetting::BTN_ACTION_BOOKMARK:
      act.pauseReadingStats();
      act.addBookmark();
      act.startPageTimer();
      break;
    default:
      break;
  }
}
