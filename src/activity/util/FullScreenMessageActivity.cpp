/**
 * @file FullScreenMessageActivity.cpp
 * @brief Definitions for FullScreenMessageActivity.
 */

#include "FullScreenMessageActivity.h"

#include <GfxRenderer.h>

#include "system/Fonts.h"

void FullScreenMessageActivity::onEnter() {
  Activity::onEnter();

  const auto height = renderer.text.getLineHeight(systemFontId());
  const auto top = (renderer.getScreenHeight() - height) / 2;

  renderer.clearScreen();
  renderer.text.centered(systemFontId(), top, text.c_str(), true, style);
  renderer.displayBuffer(refreshMode);
}
