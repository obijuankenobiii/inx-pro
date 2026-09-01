#pragma once

class GfxRenderer;

class Temperature final {
 public:
  explicit Temperature(GfxRenderer& renderer) : renderer_(renderer) {}

  void render(int x, int y, int width, int height) const;
  void preview(int x, int y, int width, int height) const;
  bool needsRefresh() const;
  void reloadPreferences() const { preferencesLoaded_ = false; }

 private:
  void renderCard(int x, int y, int width, int height, float temperature, const char* dateText, bool rising) const;

  GfxRenderer& renderer_;
  mutable bool hasTemperatureSample_ = false;
  mutable float previousTemperature_ = 0.0f;
  mutable bool temperatureRising_ = true;
  mutable bool preferencesLoaded_ = false;
  mutable bool fahrenheit_ = false;
};
