#pragma once

/**
 * @file TouchOrientation.h
 * @brief Per-device touch/panel orientation transforms.
 *
 * The GT911 reports against its panel's NATIVE frame, and that frame is not the same
 * on every board: the X4 Pro's digitizer sits 90 degrees rotated relative to the app's
 * portrait UI, while Sticky's is aligned with it. Every tap and swipe therefore needs a
 * different transform per device.
 *
 * That difference used to live inside MappedInputManager as a `#if FREEINK_DEVICE_X4PRO`
 * plus a wholesale re-shuffling of the orientation tables. Rotating those tables for the
 * X4 Pro silently rotated them for Sticky too, which is what broke Sticky's input.
 *
 * There is exactly ONE implementation of this interface in a build, chosen by
 * build_src_filter in platformio.ini:
 *     Sticky  -> TouchOrientationSticky.cpp
 *     X4 Pro  -> TouchOrientationX4Pro.cpp
 * so neither device can perturb the other and there is no runtime or preprocessor
 * branching on the hot path.
 */

#include <GfxRenderer.h>
#include <HalGPIO.h>

namespace inx {
namespace touch {

/**
 * Maps a raw panel swipe into the app's DEFAULT (unrotated) portrait frame.
 * Identity where the digitizer is already aligned with the UI.
 */
HalGPIO::TouchSwipe toDefaultOrientation(HalGPIO::TouchSwipe swipe);

/** Maps a raw panel swipe into the direction the reader is currently showing. */
HalGPIO::TouchSwipe forOrientation(GfxRenderer::Orientation orientation, HalGPIO::TouchSwipe swipe);

/**
 * Panel-native normalized tap -> logical screen coordinates.
 * Must stay the exact inverse of screenToNative(), and in lockstep with
 * GfxRenderer::rotateCoordinates().
 */
void nativeToScreen(GfxRenderer::Orientation orientation, float nativeNx, float nativeNy, float& nx, float& ny);

/** Logical screen coordinates -> panel-native normalized tap. Inverse of nativeToScreen(). */
void screenToNative(GfxRenderer::Orientation orientation, float screenNx, float screenNy, float& nx, float& ny);

}  // namespace touch
}  // namespace inx
