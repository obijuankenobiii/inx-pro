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

bool ReaderButtonBindings::handleInput(EpubActivity& act) {
  // Tap left half of the screen = previous page, right half = next page.
  // Checked first, same "stops at the first action that fires this frame"
  // rule as the physical buttons below. A tap is momentary (no press/hold
  // state to track), so this dispatches immediately rather than going
  // through handleButton()'s state machine.
  const bool touchEnabled = !act.settingsDrawer || act.settingsDrawer->isTouchEnabled();
  if (touchEnabled && act.mappedInput.hasTouch()) {
    float tapNx = 0.0f, tapNy = 0.0f;
    if (act.mappedInput.wasTouchTapInScreen(act.renderer, tapNx, tapNy)) {
      dispatch(act, tapNx < 0.5f ? SystemSetting::BTN_ACTION_PAGE_PREVIOUS : SystemSetting::BTN_ACTION_PAGE_NEXT);
      return true;
    }
  }

  // X4 Pro's physical left/right keys and Sticky's side buttons are both exposed through the shared
  // Up/Down channels. Their labels are adjusted in ButtonMappingActivity for each device.
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
    return false;  // long already fired on this press - don't also fire short on release
  }

  return false;
}

void ReaderButtonBindings::reset() {
  upState_ = PressState{};
  downState_ = PressState{};
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
      // X4 Pro has dual host framebuffers. The inactive write buffer may still
      // contain the previous page after a swap; refresh the page currently on
      // screen instead of presenting that stale buffer first.
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
