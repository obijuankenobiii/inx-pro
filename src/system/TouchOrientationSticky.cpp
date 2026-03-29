/**
 * @file TouchOrientationSticky.cpp
 * @brief Seeed reTerminal Sticky touch orientation transforms.
 *
 * Restored verbatim from commit 1485b81 ("Class clean up") — the last state before the
 * X4 Pro integration re-shuffled these tables. Sticky's GT911 is aligned with the app's
 * portrait UI, so Portrait is the identity case and the other three rotate from there.
 *
 * Do not "fix" these to match the X4 Pro. If a rotation looks wrong on the X4 Pro, edit
 * TouchOrientationX4Pro.cpp — the two are separate translation units precisely so a
 * change to one cannot reach the other.
 */

#include "TouchOrientation.h"

namespace inx {
namespace touch {

// Sticky's digitizer already matches the UI's portrait frame — nothing to pre-rotate.
HalGPIO::TouchSwipe toDefaultOrientation(const HalGPIO::TouchSwipe swipe) { return swipe; }

HalGPIO::TouchSwipe forOrientation(const GfxRenderer::Orientation orientation, const HalGPIO::TouchSwipe swipe) {
  if (swipe == HalGPIO::TouchSwipe::None) return swipe;

  // Touch reports against the fixed panel. Reader orientation rotates the content, so
  // translate the physical direction back into the direction the reader is showing. The
  // landscape tables intentionally have opposite senses: their labels describe the
  // visual content rotation.
  switch (orientation) {
    case GfxRenderer::Orientation::Portrait:
      return swipe;
    case GfxRenderer::Orientation::LandscapeClockwise:
      switch (swipe) {
        case HalGPIO::TouchSwipe::Up: return HalGPIO::TouchSwipe::Right;
        case HalGPIO::TouchSwipe::Down: return HalGPIO::TouchSwipe::Left;
        case HalGPIO::TouchSwipe::Left: return HalGPIO::TouchSwipe::Up;
        case HalGPIO::TouchSwipe::Right: return HalGPIO::TouchSwipe::Down;
        case HalGPIO::TouchSwipe::None: return HalGPIO::TouchSwipe::None;
      }
      break;
    case GfxRenderer::Orientation::PortraitInverted:
      switch (swipe) {
        case HalGPIO::TouchSwipe::Up: return HalGPIO::TouchSwipe::Down;
        case HalGPIO::TouchSwipe::Down: return HalGPIO::TouchSwipe::Up;
        case HalGPIO::TouchSwipe::Left: return HalGPIO::TouchSwipe::Right;
        case HalGPIO::TouchSwipe::Right: return HalGPIO::TouchSwipe::Left;
        case HalGPIO::TouchSwipe::None: return HalGPIO::TouchSwipe::None;
      }
      break;
    case GfxRenderer::Orientation::LandscapeCounterClockwise:
      switch (swipe) {
        case HalGPIO::TouchSwipe::Up: return HalGPIO::TouchSwipe::Left;
        case HalGPIO::TouchSwipe::Down: return HalGPIO::TouchSwipe::Right;
        case HalGPIO::TouchSwipe::Left: return HalGPIO::TouchSwipe::Down;
        case HalGPIO::TouchSwipe::Right: return HalGPIO::TouchSwipe::Up;
        case HalGPIO::TouchSwipe::None: return HalGPIO::TouchSwipe::None;
      }
      break;
  }
  return HalGPIO::TouchSwipe::None;
}

void nativeToScreen(const GfxRenderer::Orientation orientation, const float nativeNx, const float nativeNy, float& nx,
                    float& ny) {
  switch (orientation) {
    case GfxRenderer::Orientation::Portrait:
      nx = nativeNx;
      ny = nativeNy;
      break;
    case GfxRenderer::Orientation::LandscapeClockwise:
      nx = 1.0f - nativeNy;
      ny = nativeNx;
      break;
    case GfxRenderer::Orientation::PortraitInverted:
      nx = 1.0f - nativeNx;
      ny = 1.0f - nativeNy;
      break;
    case GfxRenderer::Orientation::LandscapeCounterClockwise:
      nx = nativeNy;
      ny = 1.0f - nativeNx;
      break;
  }
}

void screenToNative(const GfxRenderer::Orientation orientation, const float screenNx, const float screenNy, float& nx,
                    float& ny) {
  switch (orientation) {
    case GfxRenderer::Orientation::Portrait:
      nx = screenNx;
      ny = screenNy;
      break;
    case GfxRenderer::Orientation::LandscapeClockwise:
      nx = screenNy;
      ny = 1.0f - screenNx;
      break;
    case GfxRenderer::Orientation::PortraitInverted:
      nx = 1.0f - screenNx;
      ny = 1.0f - screenNy;
      break;
    case GfxRenderer::Orientation::LandscapeCounterClockwise:
      nx = 1.0f - screenNy;
      ny = screenNx;
      break;
  }
}

}  // namespace touch
}  // namespace inx
