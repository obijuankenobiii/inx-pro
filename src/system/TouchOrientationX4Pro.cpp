/**
 * @file TouchOrientationX4Pro.cpp
 * @brief Xteink X4 Pro touch orientation transforms.
 *
 * The X4 Pro's GT911 is mounted portrait on a landscape panel and reports 90 degrees
 * rotated relative to the app's portrait UI (BoardConfig::XTEINK_X4_PRO carries
 * swapXY=true / flipY=true for the sensor itself; this is the remaining UI-frame
 * rotation on top of that). So unlike Sticky, Portrait is NOT the identity case — every
 * table is shifted one position round, and raw swipes need a pre-rotation into the
 * default frame before any orientation handling.
 *
 * These are the transforms confirmed working on X4 Pro hardware. Sticky's live in
 * TouchOrientationSticky.cpp; the two are separate translation units so neither can
 * affect the other.
 */

#include "TouchOrientation.h"

namespace inx {
namespace touch {

HalGPIO::TouchSwipe toDefaultOrientation(const HalGPIO::TouchSwipe swipe) {
  switch (swipe) {
    case HalGPIO::TouchSwipe::Up: return HalGPIO::TouchSwipe::Right;
    case HalGPIO::TouchSwipe::Down: return HalGPIO::TouchSwipe::Left;
    case HalGPIO::TouchSwipe::Left: return HalGPIO::TouchSwipe::Up;
    case HalGPIO::TouchSwipe::Right: return HalGPIO::TouchSwipe::Down;
    case HalGPIO::TouchSwipe::None: return HalGPIO::TouchSwipe::None;
  }
  return HalGPIO::TouchSwipe::None;
}

HalGPIO::TouchSwipe forOrientation(const GfxRenderer::Orientation orientation, const HalGPIO::TouchSwipe swipe) {
  if (swipe == HalGPIO::TouchSwipe::None) return swipe;

  switch (orientation) {
    case GfxRenderer::Orientation::Portrait:
      switch (swipe) {
        case HalGPIO::TouchSwipe::Up: return HalGPIO::TouchSwipe::Right;
        case HalGPIO::TouchSwipe::Down: return HalGPIO::TouchSwipe::Left;
        case HalGPIO::TouchSwipe::Left: return HalGPIO::TouchSwipe::Up;
        case HalGPIO::TouchSwipe::Right: return HalGPIO::TouchSwipe::Down;
        case HalGPIO::TouchSwipe::None: return HalGPIO::TouchSwipe::None;
      }
      break;
    case GfxRenderer::Orientation::LandscapeClockwise:
      switch (swipe) {
        case HalGPIO::TouchSwipe::Up: return HalGPIO::TouchSwipe::Down;
        case HalGPIO::TouchSwipe::Down: return HalGPIO::TouchSwipe::Up;
        case HalGPIO::TouchSwipe::Left: return HalGPIO::TouchSwipe::Right;
        case HalGPIO::TouchSwipe::Right: return HalGPIO::TouchSwipe::Left;
        case HalGPIO::TouchSwipe::None: return HalGPIO::TouchSwipe::None;
      }
      break;
    case GfxRenderer::Orientation::PortraitInverted:
      switch (swipe) {
        case HalGPIO::TouchSwipe::Up: return HalGPIO::TouchSwipe::Left;
        case HalGPIO::TouchSwipe::Down: return HalGPIO::TouchSwipe::Right;
        case HalGPIO::TouchSwipe::Left: return HalGPIO::TouchSwipe::Down;
        case HalGPIO::TouchSwipe::Right: return HalGPIO::TouchSwipe::Up;
        case HalGPIO::TouchSwipe::None: return HalGPIO::TouchSwipe::None;
      }
      break;
    case GfxRenderer::Orientation::LandscapeCounterClockwise:
      return swipe;
  }
  return HalGPIO::TouchSwipe::None;
}

void nativeToScreen(const GfxRenderer::Orientation orientation, const float nativeNx, const float nativeNy, float& nx,
                    float& ny) {
  switch (orientation) {
    case GfxRenderer::Orientation::Portrait:
      nx = 1.0f - nativeNy;
      ny = nativeNx;
      break;
    case GfxRenderer::Orientation::LandscapeClockwise:
      nx = 1.0f - nativeNx;
      ny = 1.0f - nativeNy;
      break;
    case GfxRenderer::Orientation::PortraitInverted:
      nx = nativeNy;
      ny = 1.0f - nativeNx;
      break;
    case GfxRenderer::Orientation::LandscapeCounterClockwise:
      nx = nativeNx;
      ny = nativeNy;
      break;
  }
}

void screenToNative(const GfxRenderer::Orientation orientation, const float screenNx, const float screenNy, float& nx,
                    float& ny) {
  switch (orientation) {
    case GfxRenderer::Orientation::Portrait:
      nx = screenNy;
      ny = 1.0f - screenNx;
      break;
    case GfxRenderer::Orientation::LandscapeClockwise:
      nx = 1.0f - screenNx;
      ny = 1.0f - screenNy;
      break;
    case GfxRenderer::Orientation::PortraitInverted:
      nx = 1.0f - screenNy;
      ny = screenNx;
      break;
    case GfxRenderer::Orientation::LandscapeCounterClockwise:
      nx = screenNx;
      ny = screenNy;
      break;
  }
}

}
}
