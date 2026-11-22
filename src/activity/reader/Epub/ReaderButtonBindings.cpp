/**
 * @file ReaderButtonBindings.cpp
 * @brief Definitions for ReaderButtonBindings.
 */

#include "ReaderButtonBindings.h"

#include <Arduino.h>
#include <GfxRenderer.h>

#include <algorithm>

#include "EpubActivity.h"
#include "EpubNavigation.h"
#include "state/ReaderSetting.h"
#include "state/SystemSetting.h"
#if INX_HAS_FRONTLIGHT || FREEINK_DEVICE_X4PRO || FREEINK_DEVICE_MURPHY_M4 || FREEINK_DEVICE_PICCO
#include "system/Frontlight.h"
#endif

namespace {
uint8_t tapZoneAction(const float nx, const float ny) {
  const int column = std::min(static_cast<int>(nx * ReaderSetting::TAP_ZONE_COLUMNS),
                              static_cast<int>(ReaderSetting::TAP_ZONE_COLUMNS) - 1);
  const int row = std::min(static_cast<int>(ny * ReaderSetting::TAP_ZONE_ROWS),
                           static_cast<int>(ReaderSetting::TAP_ZONE_ROWS) - 1);
  return static_cast<uint8_t>(row * ReaderSetting::TAP_ZONE_COLUMNS + column);
}

uint8_t swipeZoneIndex(const float nx) {
  const int column = std::min(static_cast<int>(nx * ReaderSetting::SWIPE_ZONE_COLUMNS),
                              static_cast<int>(ReaderSetting::SWIPE_ZONE_COLUMNS) - 1);
  return static_cast<uint8_t>(column);
}

enum class SwipeDirection : uint8_t { Up, Down, Left, Right };
}

bool ReaderButtonBindings::handleTouchSwipe(EpubActivity& act) {
  const bool touchEnabled = !act.settingsDrawer || act.settingsDrawer->isTouchEnabled();
  if (!touchEnabled || !act.mappedInput.hasTouch()) return false;

  float startNx = 0.0f;
  float startNy = 0.0f;
  SwipeDirection direction;
  if (act.mappedInput.wasTouchSwipeUpInScreen(act.renderer, startNx, startNy)) {
    direction = SwipeDirection::Up;
  } else if (act.mappedInput.wasTouchSwipeDownInScreen(act.renderer, startNx, startNy)) {
    direction = SwipeDirection::Down;
  } else if (act.mappedInput.wasTouchSwipeLeftInScreen(act.renderer, startNx, startNy)) {
    direction = SwipeDirection::Left;
  } else if (act.mappedInput.wasTouchSwipeRightInScreen(act.renderer, startNx, startNy)) {
    direction = SwipeDirection::Right;
  } else {
    return false;
  }




  const uint8_t zone = swipeZoneIndex(startNx);
  const bool verticalDirection = direction == SwipeDirection::Up || direction == SwipeDirection::Down;
  const bool horizontalDirection = direction == SwipeDirection::Left || direction == SwipeDirection::Right;
  const bool directionAllowed = zone == 0
                                    ? (verticalDirection || direction == SwipeDirection::Right)
                                    : zone == ReaderSetting::SWIPE_ZONE_COLUMNS - 1
                                        ? (verticalDirection || direction == SwipeDirection::Left)
                                        : horizontalDirection;



  if (direction == SwipeDirection::Up && startNy >= 0.95f) return false;
  if (!directionAllowed) return false;

  uint8_t action = SystemSetting::BTN_ACTION_NONE;
  switch (direction) {
    case SwipeDirection::Up:
      action = READER_SETTINGS.swipeUpZoneActions[zone];
      break;
    case SwipeDirection::Down:
      action = READER_SETTINGS.swipeDownZoneActions[zone];
      break;
    case SwipeDirection::Left:
      action = READER_SETTINGS.swipeLeftZoneActions[zone];
      break;
    case SwipeDirection::Right:
      action = READER_SETTINGS.swipeRightZoneActions[zone];
      break;
  }
  if (action == SystemSetting::BTN_ACTION_NONE) return false;
  dispatch(act, action);
  return true;
}

bool ReaderButtonBindings::handleInput(EpubActivity& act) {
  const unsigned long now = millis();
  if (pendingDoubleTap_ && now - pendingDoubleTapAtMs_ > kDoubleTapWindowMs) {
    pendingDoubleTap_ = false;
    dispatch(act, pendingDoubleTapAction_);
    return true;
  }

  const bool touchEnabled = !act.settingsDrawer || act.settingsDrawer->isTouchEnabled();
  if (touchEnabled && act.mappedInput.hasTouch()) {
    float tapNx = 0.0f, tapNy = 0.0f;
    if (act.mappedInput.wasTouchTapInScreen(act.renderer, tapNx, tapNy)) {
      const uint8_t zone = tapZoneAction(tapNx, tapNy);
      const uint8_t configuredTapAction = READER_SETTINGS.tapZoneActions[zone];
      const uint8_t configuredDoubleTapAction = READER_SETTINGS.doubleTapZoneActions[zone];

      if (pendingDoubleTap_ && now - pendingDoubleTapAtMs_ <= kDoubleTapWindowMs &&
          pendingDoubleTapZone_ == zone && configuredDoubleTapAction != SystemSetting::BTN_ACTION_NONE) {
        pendingDoubleTap_ = false;
        dispatch(act, configuredDoubleTapAction);
        return true;
      }
      if (pendingDoubleTap_) {
        dispatch(act, pendingDoubleTapAction_);
        pendingDoubleTap_ = false;
      }

      if (configuredDoubleTapAction != SystemSetting::BTN_ACTION_NONE) {
        pendingDoubleTap_ = true;
        pendingDoubleTapAction_ = configuredTapAction;
        pendingDoubleTapZone_ = zone;
        pendingDoubleTapAtMs_ = now;
        return true;
      }

      dispatch(act, configuredTapAction);
      return true;
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
  pendingDoubleTapAction_ = SystemSetting::BTN_ACTION_NONE;
  pendingDoubleTapZone_ = 0;
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
#if INX_HAS_FRONTLIGHT || FREEINK_DEVICE_X4PRO || FREEINK_DEVICE_MURPHY_M4 || FREEINK_DEVICE_PICCO
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
    case SystemSetting::BTN_ACTION_EXIT:
      act.endPageTimer();
      act.onGoBack();
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
      act.renderer.displayBuffer(FREEINK_DEVICE_X4PRO || FREEINK_DEVICE_MURPHY_M4 ? HalDisplay::FULL_REFRESH
                                                                                     : HalDisplay::MANUAL_REFRESH);
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
