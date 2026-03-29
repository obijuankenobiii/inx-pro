#include "Temperature.h"

#include <GfxRenderer.h>
#include <HalGPIO.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "images/TemperatureDown.h"
#include "images/TemperatureUp.h"
#include "system/Fonts.h"

extern HalGPIO gpio;

namespace {

constexpr const char* kMonths[] = {"",       "January", "February", "March",    "April",      "May",
                                   "June",   "July",    "August",   "September", "October",   "November",
                                   "December"};

}  // namespace

void Temperature::renderCard(const int x, const int y, const int width, const int height, const float temperature,
                              const char* dateText, const bool rising) const {
  if (width <= 0 || height <= 0) return;

  // Keep the card on paper so it matches the light reference design instead of
  // rendering a gray checkerboard background on the e-paper display.
  renderer_.rectangle.fill(x, y, width, height, false, true, true);

  const int padding = std::max(16, std::min(26, width / 10));
  const int dateFont = MONTSERRAT_10_FONT_ID;
  const int labelFont = MONTSERRAT_14_FONT_ID;
  const int dateLineHeight = renderer_.text.getLineHeight(dateFont);
  const int labelLineHeight = renderer_.text.getLineHeight(labelFont);
  const bool wideCard = width >= 300 && height >= 220;
  const int valueFont = wideCard ? MONTSERRAT_CLOCK_70_FONT_ID : MONTSERRAT_16_FONT_ID;
  constexpr int kClockVisibleHeight = 106;
  const int valueHeight = wideCard ? kClockVisibleHeight : renderer_.text.getLineHeight(valueFont);
  const int contentHeight = dateLineHeight + 6 + labelLineHeight + 30 + valueHeight;
  const int contentTop = y + std::max(0, (height - contentHeight) / 2);
  const int dateY = contentTop;
  renderer_.text.render(dateFont, x + padding, dateY, dateText, true);

  const int labelY = dateY + dateLineHeight + 6;
  renderer_.text.renderGray(labelFont, x + padding, labelY, "Temperature", true, EpdFontFamily::BOLD);

  char temperatureValue[20];
  std::snprintf(temperatureValue, sizeof(temperatureValue), "%.1f", static_cast<double>(temperature));

  const int valueTop = labelY + labelLineHeight + 30;

  if (wideCard) {
    // Montserrat Clock intentionally contains only clock glyphs (0-9 and :).
    // Render its digits for the large value, then draw the decimal point and
    // Celsius unit separately so the decimal remains visible.
    char integerValue[16] = {};
    char fractionalValue[4] = {};
    const char* decimal = std::strchr(temperatureValue, '.');
    if (decimal != nullptr) {
      const size_t integerLength = static_cast<size_t>(decimal - temperatureValue);
      std::memcpy(integerValue, temperatureValue, std::min(integerLength, sizeof(integerValue) - 1));
      std::snprintf(fractionalValue, sizeof(fractionalValue), "%s", decimal + 1);
    } else {
      std::snprintf(integerValue, sizeof(integerValue), "%s", temperatureValue);
    }

    constexpr int kClockGlyphTop = 104;
    const int clockTopInset = renderer_.text.getFontAscenderSize(valueFont) - kClockGlyphTop;
    const int valueY = valueTop - clockTopInset;
    int valueX = x + padding;
    renderer_.text.render(valueFont, valueX, valueY, integerValue, true, EpdFontFamily::BOLD);
    valueX += renderer_.text.getWidth(valueFont, integerValue, EpdFontFamily::BOLD) + 3;

    const int decimalY = valueY + clockTopInset + 98;
    renderer_.circle.render(valueX + 4, decimalY, 4, true);
    valueX += 12;
    renderer_.text.render(valueFont, valueX, valueY, fractionalValue, true, EpdFontFamily::BOLD);

    const int clockBottom = valueY + clockTopInset + kClockGlyphTop + 2;
    const int unitFont = MONTSERRAT_14_FONT_ID;
    const int unitY = clockBottom - renderer_.text.getLineHeight(unitFont) + 3;
    valueX += renderer_.text.getWidth(valueFont, fractionalValue, EpdFontFamily::BOLD) + 8;
    renderer_.text.renderGray(unitFont, valueX, unitY, "°C", true, EpdFontFamily::BOLD);
  } else {
    char temperatureNumber[16];
    std::snprintf(temperatureNumber, sizeof(temperatureNumber), "%.1f", static_cast<double>(temperature));
    renderer_.text.render(valueFont, x + padding, valueTop, temperatureNumber, true, EpdFontFamily::BOLD);
    const int numberWidth = renderer_.text.getWidth(valueFont, temperatureNumber, EpdFontFamily::BOLD);
    const int unitFont = MONTSERRAT_14_FONT_ID;
    const int unitY = valueTop + renderer_.text.getLineHeight(valueFont) - renderer_.text.getLineHeight(unitFont);
    renderer_.text.renderGray(unitFont, x + padding + numberWidth + 6, unitY, "°C", true, EpdFontFamily::BOLD);
  }

  const int iconSize = std::max(40, std::min(92, std::min(width / 3, height - padding * 2)));
  const int iconX = x + width - padding - iconSize;
  const int iconY = y + std::max(padding, (height - iconSize) / 2);
  renderer_.bitmap.iconScaled(rising ? TemperatureUp : TemperatureDown, iconX, iconY, 114, 116, iconSize, iconSize);
}

void Temperature::render(const int x, const int y, const int width, const int height) const {
  float temperature = 0.0f;
  float humidity = 0.0f;
  bool available = false;
#ifndef SIMULATOR
  available = gpio.getTemperatureAndHumidity(temperature, humidity);
#endif

  if (!available) {
    renderer_.rectangle.fill(x, y, width, height, false);
    renderer_.text.render(MONTSERRAT_10_FONT_ID, x + 16, y + std::max(0, height / 2 - 16),
                          "Temperature unavailable", true, EpdFontFamily::BOLD);
    renderer_.text.render(MONTSERRAT_8_FONT_ID, x + 16, y + height / 2 + 12, "Sensor not found", true);
    return;
  }

  bool rising = temperatureRising_;
  if (hasTemperatureSample_) {
    if (temperature > previousTemperature_ + 0.1f) rising = true;
    if (temperature < previousTemperature_ - 0.1f) rising = false;
  }
  previousTemperature_ = temperature;
  temperatureRising_ = rising;
  hasTemperatureSample_ = true;

  char dateText[32] = "Indoor temperature";
#ifndef SIMULATOR
  HalGPIO::DateTime dateTime;
  if (gpio.readDateTime(dateTime) && dateTime.month >= 1 && dateTime.month <= 12) {
    std::snprintf(dateText, sizeof(dateText), "%02u %s %04u", static_cast<unsigned>(dateTime.day),
                  kMonths[dateTime.month], static_cast<unsigned>(dateTime.year));
  }
#endif

  renderCard(x, y, width, height, temperature, dateText, rising);
}

void Temperature::preview(const int x, const int y, const int width, const int height) const {
  renderCard(x, y, width, height, 18.0f, "04 August 2024", true);
}

bool Temperature::needsRefresh() const {
#ifdef SIMULATOR
  return false;
#else
  float temperature = 0.0f;
  float humidity = 0.0f;
  if (!gpio.getTemperatureAndHumidity(temperature, humidity)) return false;
  return !hasTemperatureSample_ || std::fabs(temperature - previousTemperature_) >= 0.1f;
#endif
}
