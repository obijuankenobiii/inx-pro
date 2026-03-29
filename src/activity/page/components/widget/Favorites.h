#pragma once

#include <string>
#include <vector>

#include "BaseCarousel.h"
#include "state/BookState.h"

/** White home widget showing favorite-book covers in a clipped horizontal carousel. */
class Favorites final : public BaseCarousel {
 public:
  explicit Favorites(GfxRenderer& renderer) : BaseCarousel(renderer) {}

  void render(int index, int x, int y, int width, int height, bool background = false,
              HomeTheme::CarouselStyle style = HomeTheme::CarouselStyle::Left, bool showLabel = true,
              HomeTheme::CarouselLabelColor labelColor = HomeTheme::CarouselLabelColor::Black,
              HomeTheme::CarouselShadowStyle shadowStyle = HomeTheme::CarouselShadowStyle::None) const;
  void preview(int x, int y, int width, int height, bool background = false,
               HomeTheme::CarouselStyle style = HomeTheme::CarouselStyle::Left, bool showLabel = true,
               HomeTheme::CarouselLabelColor labelColor = HomeTheme::CarouselLabelColor::Black,
               HomeTheme::CarouselShadowStyle shadowStyle = HomeTheme::CarouselShadowStyle::None) const;
  int hitTest(int index, int x, int y, int areaX, int areaY, int areaW, int areaH,
              HomeTheme::CarouselStyle style = HomeTheme::CarouselStyle::Left, bool showLabel = true,
              HomeTheme::CarouselLabelColor labelColor = HomeTheme::CarouselLabelColor::Black) const;
  void invalidate() const;
  int count() const;
  const std::string& pathAt(int index) const;

 private:
  struct CardBounds {
    int x;
    int y;
    int width;
    int height;
  };

  void load() const;
  void sourceDimensions(const BookState::Book& book, int& width, int& height) const;
  int cardWidth(const BookState::Book& book, int width, int height) const;
  CardBounds cardBounds(const BookState::Book& book, int cardX, int y, int width, int height) const;
  void renderCover(const BookState::Book& book, const CardBounds& bounds, bool cropToVisibleWidth,
                   HomeTheme::CarouselShadowStyle shadowStyle, float cropAnchorX = 0.0f) const;
  std::string thumbnailPath(const std::string& bookPath) const;
  void renderCentered(int index, int x, int y, int width, int height, HomeTheme::CarouselShadowStyle shadowStyle) const;
  void previewCentered(int x, int y, int width, int height, HomeTheme::CarouselShadowStyle shadowStyle) const;
  int hitTestCentered(int index, int x, int y, int areaX, int areaY, int areaW, int areaH) const;

  mutable bool loaded_ = false;
  mutable std::vector<BookState::Book> books_;
};
