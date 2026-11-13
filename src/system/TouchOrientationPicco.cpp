/**
 * @file TouchOrientationPicco.cpp
 * @brief BOOX/Onyx Picco touch orientation transforms.
 *
 * Picco's TMA525C reports the touch frame rotated 90 degrees relative to the
 * logical portrait UI. Apply the panel-to-portrait transform globally here.
 */

#include "TouchOrientation.h"

#if FREEINK_DEVICE_PICCO

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
      switch (swipe) {
        case HalGPIO::TouchSwipe::Up: return HalGPIO::TouchSwipe::Up;
        case HalGPIO::TouchSwipe::Down: return HalGPIO::TouchSwipe::Down;
        case HalGPIO::TouchSwipe::Left: return HalGPIO::TouchSwipe::Left;
        case HalGPIO::TouchSwipe::Right: return HalGPIO::TouchSwipe::Right;
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

}  // namespace touch
}  // namespace inx

#endif  // FREEINK_DEVICE_PICCO
