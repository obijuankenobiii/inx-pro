#pragma once

class GfxRenderer;

class Calendar final {
 public:
  explicit Calendar(GfxRenderer& renderer) : renderer_(renderer) {}

  void render(int x, int y, int width, int height) const;
  bool needsRefresh() const;

 private:
  bool readDate(int& year, int& month, int& day, int& weekday) const;

  GfxRenderer& renderer_;
  mutable int renderedDateKey_ = 0;
};
