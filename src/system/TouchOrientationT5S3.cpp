/**
 * @file TouchOrientationT5S3.cpp
 * @brief LilyGo T5 S3 touch orientation transforms.
 *
 * BoardConfig::LILYGO_T5_PRO_GT911 already swaps the GT911's portrait 540x960 frame into
 * the panel-native landscape 960x540 frame (swapXY=true / flipX=false / flipY=true). So,
 * as on the X4 Pro, InputManager reports touches in framebuffer coordinates and these are
 * the exact inverse of GfxRenderer::rotateCoordinates(): Portrait is NOT the identity case,
 * and raw swipes need a pre-rotation into the default frame.
 *
 * The tables start as a copy of TouchOrientationX4Pro.cpp because that math depends only
 * on "touch arrives in framebuffer coordinates", not on the board. They are a separate
 * translation unit so a T5-specific correction, if one is ever needed, cannot reach the
 * X4 Pro. Confirmed unchanged on a T5 S3 Pro Lite.
 */

#include "TouchOrientation.h"

namespace inx {
namespace touch {

HalGPIO::TouchSwipe toDefaultOrientation(const HalGPIO::TouchSwipe swipe) {
  switch (swipe) {
    case HalGPIO::TouchSwipe::Up:
      return HalGPIO::TouchSwipe::Right;
    case HalGPIO::TouchSwipe::Down:
      return HalGPIO::TouchSwipe::Left;
    case HalGPIO::TouchSwipe::Left:
      return HalGPIO::TouchSwipe::Up;
    case HalGPIO::TouchSwipe::Right:
      return HalGPIO::TouchSwipe::Down;
    case HalGPIO::TouchSwipe::None:
      return HalGPIO::TouchSwipe::None;
  }
  return HalGPIO::TouchSwipe::None;
}

HalGPIO::TouchSwipe forOrientation(const GfxRenderer::Orientation orientation, const HalGPIO::TouchSwipe swipe) {
  if (swipe == HalGPIO::TouchSwipe::None) return swipe;

  switch (orientation) {
    case GfxRenderer::Orientation::Portrait:
      switch (swipe) {
        case HalGPIO::TouchSwipe::Up:
          return HalGPIO::TouchSwipe::Right;
        case HalGPIO::TouchSwipe::Down:
          return HalGPIO::TouchSwipe::Left;
        case HalGPIO::TouchSwipe::Left:
          return HalGPIO::TouchSwipe::Up;
        case HalGPIO::TouchSwipe::Right:
          return HalGPIO::TouchSwipe::Down;
        case HalGPIO::TouchSwipe::None:
          return HalGPIO::TouchSwipe::None;
      }
      break;
    case GfxRenderer::Orientation::LandscapeClockwise:
      switch (swipe) {
        case HalGPIO::TouchSwipe::Up:
          return HalGPIO::TouchSwipe::Down;
        case HalGPIO::TouchSwipe::Down:
          return HalGPIO::TouchSwipe::Up;
        case HalGPIO::TouchSwipe::Left:
          return HalGPIO::TouchSwipe::Right;
        case HalGPIO::TouchSwipe::Right:
          return HalGPIO::TouchSwipe::Left;
        case HalGPIO::TouchSwipe::None:
          return HalGPIO::TouchSwipe::None;
      }
      break;
    case GfxRenderer::Orientation::PortraitInverted:
      switch (swipe) {
        case HalGPIO::TouchSwipe::Up:
          return HalGPIO::TouchSwipe::Left;
        case HalGPIO::TouchSwipe::Down:
          return HalGPIO::TouchSwipe::Right;
        case HalGPIO::TouchSwipe::Left:
          return HalGPIO::TouchSwipe::Down;
        case HalGPIO::TouchSwipe::Right:
          return HalGPIO::TouchSwipe::Up;
        case HalGPIO::TouchSwipe::None:
          return HalGPIO::TouchSwipe::None;
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

}  // namespace touch
}  // namespace inx
