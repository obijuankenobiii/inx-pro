#pragma once

/**
 * @file MappedInputManager.h
 * @brief Public interface and types for MappedInputManager.
 */

#include <HalGPIO.h>

#include <cstdint>

class GfxRenderer;

class MappedInputManager {
 public:
  enum class Button { Back, Confirm, Left, Right, Up, Down, Power, PageBack, PageForward };
  enum class MotionGesture : uint8_t { None, Previous, Next };

  struct Labels {
    const char* btn1;
    const char* btn2;
    const char* btn3;
    const char* btn4;
  };

  /** Labels for the physical page (side) buttons, top then bottom, per Side Button Layout setting. */
  struct SideLabels {
    const char* top;
    const char* bottom;
  };

  explicit MappedInputManager(HalGPIO& gpio) : gpio(gpio) {}

  /**
   * When true, Up/Down, Left/Right, and PageBack/PageForward are swapped before GPIO lookup.
   * Use with GfxRenderer::LandscapeClockwise (180° vs panel) so physical directions match the
   * rotated framebuffer; clear when leaving that mode or the reader.
   */
  void setInvertDirectionalAxes180(bool invert) { invertDirectionalAxes180_ = invert; }
  bool invertDirectionalAxes180() const { return invertDirectionalAxes180_; }

  bool wasPressed(Button button) const;
  bool wasReleased(Button button) const;
  bool isPressed(Button button) const;
  bool wasAnyPressed() const;
  bool wasAnyReleased() const;
  MotionGesture readMotionGesture(uint8_t orientation, uint8_t mode, uint8_t sensitivity) const;
  unsigned long getHeldTime() const;

  /** Raw GPIO read (layout + invert still apply to HalGPIO indices). For fixed chords use HalGPIO::BTN_* ). */
  bool rawHalIsPressed(uint8_t halButtonIndex) const;

  /**
   * Raw touch passthrough for activities that hit-test taps against their own
   * layout (e.g. "open the grid item under the tap") instead of mapped button
   * edges. False/no-op on hardware with no touch panel. nx/ny are normalized
   * 0..1 in the panel's native (unrotated) frame.
   */
  bool hasTouch() const { return pendingTouchTap_ || gpio.hasTouch(); }
  bool isTouchPressed() const { return gpio.isTouchPressed(); }
  bool wasTouchPressedInScreen(const GfxRenderer& renderer, float& nx, float& ny) const;
  bool isTouchHeldInScreen(const GfxRenderer& renderer, float& nx, float& ny) const;
  bool wasTouchTap(float& nx, float& ny) const { return gpio.wasTouchTap(nx, ny); }
  unsigned long lastTouchHeldMs() const { return gpio.lastTouchHeldMs(); }
  bool wasTouchSwipeUp() const { return swipeForDefaultOrientation() == HalGPIO::TouchSwipe::Up; }
  bool wasTouchSwipeDown() const { return swipeForDefaultOrientation() == HalGPIO::TouchSwipe::Down; }
  bool wasTouchSwipeLeft() const { return swipeForDefaultOrientation() == HalGPIO::TouchSwipe::Left; }
  bool wasTouchSwipeRight() const { return swipeForDefaultOrientation() == HalGPIO::TouchSwipe::Right; }
  /** Swipe directions relative to the reader content, rather than the fixed touch panel. */
  bool wasTouchSwipeUpForRenderer(const GfxRenderer& renderer) const;
  bool wasTouchSwipeDownForRenderer(const GfxRenderer& renderer) const;
  bool wasTouchSwipeLeftForRenderer(const GfxRenderer& renderer) const;
  bool wasTouchSwipeRightForRenderer(const GfxRenderer& renderer) const;
  bool wasTouchSwipeUpInScreen(const GfxRenderer& renderer, float& nx, float& ny) const;
  bool wasTouchSwipeLeftInScreen(const GfxRenderer& renderer, float& nx, float& ny) const;
  bool wasTouchSwipeRightInScreen(const GfxRenderer& renderer, float& nx, float& ny) const;
  /** Down-swipe plus where it STARTED, in logical screen coords — lets a caller require
   *  the gesture to begin in a particular band (e.g. only from the top edge). */
  bool wasTouchSwipeDownInScreen(const GfxRenderer& renderer, float& nx, float& ny) const;
  /** Map a panel-native tap into the renderer's current logical screen frame. */
  bool wasTouchTapInScreen(const GfxRenderer& renderer, float& nx, float& ny) const;
  /** Puts a consumed tap back for a child activity after a parent hit-test. */
  void restoreTouchTap(float nx, float ny) const {
    pendingTouchTap_ = true;
    pendingTouchNx_ = nx;
    pendingTouchNy_ = ny;
  }
  /** Restores a tap that was already mapped into the renderer's logical frame. */
  void restoreTouchTapInScreen(const GfxRenderer& renderer, float nx, float ny) const;
  /** Discards a buffered tap before an activity transition. */
  void discardPendingTouchTap() const { pendingTouchTap_ = false; }
  /** Discards a latched swipe before an activity transition, so the gesture that ended one
   *  activity is not handled again by the next one. */
#if FREEINK_DEVICE_X4PRO
  void discardPendingSwipe() const { gpio.discardTouchSwipe(); }
#else
  void discardPendingSwipe() const {}
#endif
  /** Swallow a touch still held across an activity transition, including its release. */
#if FREEINK_DEVICE_X4PRO
  void ignoreCurrentTouch() const { gpio.ignoreCurrentTouchUntilRelease(); }
#else
  void ignoreCurrentTouch() const {}
#endif
  /** Consumes a tap and converts the shared page-header back icon into a Back edge. */
  bool consumeHeaderBackTap(const GfxRenderer& renderer) const;
  /** Injects one Back press/release edge for a touch-driven header action. */
  void injectButtonEdge(Button button) const;

  Labels mapLabels(const char* back, const char* confirm, const char* previous, const char* next) const;

  /**
   * Like mapLabels, but Left/Right slot text follows Settings → Next & Previous Mapping and drawer
   * orientation (portrait vs landscape list uses different prev/next buttons). Used for TOC lists.
   */
  Labels mapLabelsWithReaderNav(const char* back, const char* confirm, const char* prevSym, const char* nextSym,
                                bool landscapeDrawer) const;

  /** « / » order follows which GPIO is wired as page-back vs page-forward (see Side Button Layout). */
  SideLabels mapSideLabels() const;

 private:
  HalGPIO& gpio;
  bool invertDirectionalAxes180_ = false;

  bool mapButton(Button button, bool (HalGPIO::*fn)(uint8_t) const) const;
  HalGPIO::TouchSwipe swipeForDefaultOrientation() const;
  HalGPIO::TouchSwipe swipeForRenderer(const GfxRenderer& renderer) const;
  bool readTouchTapNative(float& nx, float& ny) const;
  void mapNativeTouchToScreen(const GfxRenderer& renderer, float nativeNx, float nativeNy, float& nx, float& ny) const;
  void mapScreenTouchToNative(const GfxRenderer& renderer, float screenNx, float screenNy, float& nx,
                              float& ny) const;

  mutable bool pendingTouchTap_ = false;
  mutable float pendingTouchNx_ = 0.0f;
  mutable float pendingTouchNy_ = 0.0f;
  mutable int8_t injectedPressedButton_ = -1;
  mutable int8_t injectedReleasedButton_ = -1;
};
