#include "BaseCarousel.h"

#include <GfxRenderer.h>

#include <algorithm>

#include "system/Fonts.h"

void BaseCarousel::renderBackground(const int x, const int y, const int width, const int height,
                                    const bool background) const {
  if (background) {
    for (int py = (y + 1) & ~1; py < y + height; py += 2) {
      for (int px = (x + 1) & ~1; px < x + width; px += 2) renderer_.drawPixel(px, py, true);
    }
  } else {
    renderer_.rectangle.fill(x, y, width, height, false);
  }
}

void BaseCarousel::renderShadow(GfxRenderer& renderer, const int x, const int y, const int width, const int height,
                                const HomeTheme::CarouselShadowStyle style) {
  if (width <= 0 || height <= 0) return;
  if (style == HomeTheme::CarouselShadowStyle::None) return;
  if (style == HomeTheme::CarouselShadowStyle::Black) {
    renderer.rectangle.fill(x, y, width, height, static_cast<int>(GfxRenderer::FillTone::Ink));
    return;
  }
  renderer.rectangle.fill(x, y, width, height, static_cast<int>(GfxRenderer::FillTone::Gray));
}

BaseCarousel::ContentArea BaseCarousel::contentArea(const int y, const int height, const bool showLabel) const {
  if (!showLabel) return {y, height};
  constexpr int labelGap = 10;
  const int labelHeight = renderer_.text.getLineHeight(systemFontId());
  const int reserved = labelHeight + labelGap;
  return {y + reserved, std::max(24, height - reserved)};
}

void BaseCarousel::renderLabel(const int x, const int y, const char* label,
                               const HomeTheme::CarouselLabelColor color) const {
  constexpr int leftPadding = 20;
  constexpr int topPadding = 5;
  if (color == HomeTheme::CarouselLabelColor::Gray) {
    renderer_.text.renderGray(systemFontId(), x + leftPadding, y + topPadding, label, true, EpdFontFamily::BOLD);
  } else {
    renderer_.text.render(systemFontId(), x + leftPadding, y + topPadding, label, true, EpdFontFamily::BOLD);
  }
}
