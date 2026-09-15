#include "Button.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <string>

#include "system/Fonts.h"
#include "system/LanguageManager.h"

int Button::width(const GfxRenderer& renderer, const char* label, const int font) {
  const char* translatedLabel = LanguageManager::translateText(label ? label : "");
  return renderer.text.getUntranslatedWidth(font, translatedLabel ? translatedLabel : "") + horizontalPadding * 2;
}

void Button::render(const GfxRenderer& renderer, const ButtonBounds& bounds, const char* label, const bool fill,
                   const int font) {
  renderer.rectangle.fill(bounds.x, bounds.y, bounds.width, bounds.height, fill);
  renderer.rectangle.render(bounds.x, bounds.y, bounds.width, bounds.height, true);

  const char* translatedLabel = LanguageManager::translateText(label ? label : "");
  const std::string text = renderer.text.truncate(font, translatedLabel ? translatedLabel : "",
                                                  std::max(1, bounds.width - horizontalPadding * 2));
  const int textWidth = renderer.text.getUntranslatedWidth(font, text.c_str());
  const int textY = bounds.y + (bounds.height - renderer.text.getLineHeight(font)) / 2;
  const int textX = bounds.x + (bounds.width - textWidth) / 2;
  renderer.text.renderUntranslated(font, textX, textY, text.c_str(), !fill, EpdFontFamily::REGULAR);
}
