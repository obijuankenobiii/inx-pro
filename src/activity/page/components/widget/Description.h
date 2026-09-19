#pragma once

#include <EpdFontFamily.h>

#include "BaseCarousel.h"

#include <string>
#include <vector>

struct DescriptionWidgetLine {
  struct Run {
    std::string text;
    EpdFontFamily::Style style = EpdFontFamily::REGULAR;
  };

  std::vector<Run> runs;
};

/** Displays the metadata description for the currently selected recent book. */
class Description final : public BaseCarousel {
 public:
  explicit Description(GfxRenderer& renderer) : BaseCarousel(renderer) {}

  void render(int recentIndex, int x, int y, int width, int height, bool background = false,
              bool showLabel = false, HomeTheme::CarouselLabelColor labelColor = HomeTheme::CarouselLabelColor::Black,
              HomeTheme::CarouselShadowStyle shadowStyle = HomeTheme::CarouselShadowStyle::None,
              bool showTitle = true, bool showAuthor = true, bool showProgress = true,
              bool showRating = true) const;

 private:
  void ensureLines(int recentIndex, int width) const;

  mutable std::string cachedPath_;
  mutable int cachedWidth_ = -1;
  mutable int cachedRatingStars_ = 0;
  mutable std::vector<DescriptionWidgetLine> lines_;
};
