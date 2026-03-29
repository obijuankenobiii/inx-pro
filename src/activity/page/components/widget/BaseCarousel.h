#pragma once

#include "state/HomeTheme.h"

class GfxRenderer;

/** Shared shell and style contract for the Recent and Favorite carousel widgets. */
class BaseCarousel {
 public:
  static void renderShadow(GfxRenderer& renderer, int x, int y, int width, int height,
                           HomeTheme::CarouselShadowStyle style);

 protected:
  struct ContentArea {
    int y;
    int height;
  };

  explicit BaseCarousel(GfxRenderer& renderer) : renderer_(renderer) {}

  void renderBackground(int x, int y, int width, int height, bool background) const;
  ContentArea contentArea(int y, int height, bool showLabel) const;
  void renderLabel(int x, int y, const char* label, HomeTheme::CarouselLabelColor color) const;

  GfxRenderer& renderer_;
};
