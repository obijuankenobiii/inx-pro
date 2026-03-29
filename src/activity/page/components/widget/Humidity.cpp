#include "Humidity.h"

#include <GfxRenderer.h>
#include <HalGPIO.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "images/Humidity.h"
#include "system/Fonts.h"

extern HalGPIO gpio;

void Humidity::renderCard(const int x, const int y, const int width, const int height, const float humidity) const {
  if (width <= 0 || height <= 0) return;

  renderer_.rectangle.fill(x, y, width, height, false, true, true);

  const int padding = std::max(16, std::min(26, width / 10));
  const int labelFont = MONTSERRAT_14_FONT_ID;
  const int labelLineHeight = renderer_.text.getLineHeight(labelFont);
  const bool wideCard = width >= 300 && height >= 220;
  const int valueFont = wideCard ? MONTSERRAT_CLOCK_70_FONT_ID : MONTSERRAT_16_FONT_ID;
  constexpr int kClockVisibleHeight = 106;
  const int valueHeight = wideCard ? kClockVisibleHeight : renderer_.text.getLineHeight(valueFont);
  const int contentHeight = labelLineHeight + 30 + valueHeight;
  const int contentTop = y + std::max(0, (height - contentHeight) / 2);
  const int labelY = contentTop;
  renderer_.text.renderGray(labelFont, x + padding, labelY, "Humidity", true, EpdFontFamily::BOLD);

  char humidityValue[16];
  std::snprintf(humidityValue, sizeof(humidityValue), "%.0f", static_cast<double>(humidity));
  const int valueTop = labelY + labelLineHeight + 30;

  if (wideCard) {
    constexpr int kClockGlyphTop = 104;
    const int clockTopInset = renderer_.text.getFontAscenderSize(valueFont) - kClockGlyphTop;
    const int valueY = valueTop - clockTopInset;
    const int valueX = x + padding;
    renderer_.text.render(valueFont, valueX, valueY, humidityValue, true, EpdFontFamily::BOLD);

    const int clockBottom = valueY + clockTopInset + kClockGlyphTop + 2;
    const int unitFont = MONTSERRAT_14_FONT_ID;
    const int unitY = clockBottom - renderer_.text.getLineHeight(unitFont) + 3;
    const int unitX = valueX + renderer_.text.getWidth(valueFont, humidityValue, EpdFontFamily::BOLD) + 8;
    renderer_.text.renderGray(unitFont, unitX, unitY, "%", true, EpdFontFamily::BOLD);
  } else {
    renderer_.text.render(valueFont, x + padding, valueTop, humidityValue, true, EpdFontFamily::BOLD);
    const int numberWidth = renderer_.text.getWidth(valueFont, humidityValue, EpdFontFamily::BOLD);
    const int unitFont = MONTSERRAT_14_FONT_ID;
    const int unitY = valueTop + renderer_.text.getLineHeight(valueFont) - renderer_.text.getLineHeight(unitFont);
    renderer_.text.renderGray(unitFont, x + padding + numberWidth + 6, unitY, "%", true, EpdFontFamily::BOLD);
  }

  const int iconSize = std::max(40, std::min(92, std::min(width / 3, height - padding * 2)));
  const int iconX = x + width - padding - iconSize;
  const int iconY = y + std::max(padding, (height - iconSize) / 2);
  renderer_.bitmap.iconScaled(HumidityIcon, iconX, iconY, 118, 120, iconSize, iconSize);
}

void Humidity::render(const int x, const int y, const int width, const int height) const {
  float temperature = 0.0f;
  float humidity = 0.0f;
  bool available = false;
#ifndef SIMULATOR
  available = gpio.getTemperatureAndHumidity(temperature, humidity);
#endif

  if (!available) {
    renderer_.rectangle.fill(x, y, width, height, false);
    renderer_.text.render(MONTSERRAT_10_FONT_ID, x + 16, y + std::max(0, height / 2 - 16),
                          "Humidity unavailable", true, EpdFontFamily::BOLD);
    renderer_.text.render(MONTSERRAT_8_FONT_ID, x + 16, y + height / 2 + 12, "Sensor not found", true);
    return;
  }

  previousHumidity_ = humidity;
  hasHumiditySample_ = true;
  renderCard(x, y, width, height, humidity);
}

void Humidity::preview(const int x, const int y, const int width, const int height) const {
  renderCard(x, y, width, height, 55.0f);
}

bool Humidity::needsRefresh() const {
#ifdef SIMULATOR
  return false;
#else
  float temperature = 0.0f;
  float humidity = 0.0f;
  if (!gpio.getTemperatureAndHumidity(temperature, humidity)) return false;
  return !hasHumiditySample_ || std::fabs(humidity - previousHumidity_) >= 1.0f;
#endif
}
