#pragma once

/**
 * @file ReaderButtonBindings.h
 * @brief Reader button action dispatch for the EPUB reader.
 *
 * Self-contained wrapper (same shape as EpubDictionaryUi/EpubAnnotationUi: a `friend class` of
 * EpubActivity that reaches back into its private helpers) so button handling doesn't spread through
 * EpubActivity.cpp. The X4 Pro's left/right and Sticky's Up/Down buttons use the Reader Button Mapping
 * settings; the generic dispatcher remains shared with the power button and Quick Actions popup.
 */

#include <cstdint>

#include "system/MappedInputManager.h"

class EpubActivity;

class ReaderButtonBindings {
 public:
  ReaderButtonBindings() = default;

  /** Call once per loop after any chord-entry/active-overlay checks. Returns true if an action fired. */
  bool handleInput(EpubActivity& act);

  /** Fires a single READER_BUTTON_ACTION immediately, bypassing press-state tracking. Public so
   *  EpubActivity's Power button (short-press only, no long-press pairing so it doesn't need
   *  handleButton()'s state machine) can reuse the same action dispatch as Up/Down
   *  instead of duplicating a subset of it. */
  void dispatch(EpubActivity& act, uint8_t action);

  /** Clears the fixed Up/Down press-state tracking used by non-X4 Pro readers. */
  void reset();

 private:
  struct PressState {
    bool active = false;
    unsigned long pressStartMs = 0;
    bool longFired = false;
  };

  bool handleButton(EpubActivity& act, MappedInputManager::Button button, PressState& state, uint8_t shortAction,
                    uint8_t longAction);

  PressState upState_;
  PressState downState_;
  bool pendingDoubleTap_ = false;
  bool pendingDoubleTapForward_ = false;
  unsigned long pendingDoubleTapAtMs_ = 0;
  static constexpr unsigned long kLongPressMs = 700;
  static constexpr unsigned long kDoubleTapWindowMs = 400;
};
