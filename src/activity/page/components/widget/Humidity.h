#pragma once

class GfxRenderer;

class Humidity final {
 public:
  explicit Humidity(GfxRenderer& renderer) : renderer_(renderer) {}

  void render(int x, int y, int width, int height) const;
  void preview(int x, int y, int width, int height) const;
  bool needsRefresh() const;

 private:
  void renderCard(int x, int y, int width, int height, float humidity) const;

  GfxRenderer& renderer_;
  mutable bool hasHumiditySample_ = false;
  mutable float previousHumidity_ = 0.0f;
};
