#include "TodaysReading.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "state/ReadingGoal.h"
#include "state/RecentBooks.h"
#include "system/Fonts.h"

namespace {

constexpr float kPi = 3.14159265f;
constexpr int kDesignHeight = 300;

float normalizedAngle(float angle) {
  constexpr float twoPi = 2.0f * kPi;
  while (angle < 0.0f) angle += twoPi;
  while (angle >= twoPi) angle -= twoPi;
  return angle;
}

float angleDelta(const float angle, const float start) {
  constexpr float twoPi = 2.0f * kPi;
  float delta = normalizedAngle(angle) - normalizedAngle(start);
  if (delta < 0.0f) delta += twoPi;
  return delta;
}

bool insideSweep(const float angle, const float start, const float sweep) {
  constexpr float twoPi = 2.0f * kPi;
  if (sweep >= twoPi - 0.002f) return true;
  return angleDelta(angle, start) <= sweep + 0.008f;
}

void fillAnnulusWedge(const GfxRenderer& renderer, const int cx, const int cy, const int outerRadius,
                      const int innerRadius, const float startAngle, const float sweep) {
  if (innerRadius >= outerRadius || sweep <= 0.0f) return;
  constexpr float twoPi = 2.0f * kPi;
  const bool full = sweep >= twoPi - 0.002f;
  const long innerSquared = static_cast<long>(innerRadius) * innerRadius;
  const long outerSquared = static_cast<long>(outerRadius) * outerRadius;

  for (int py = cy - outerRadius; py <= cy + outerRadius; py++) {
    const long dy = py - cy;
    const long dySquared = dy * dy;
    for (int px = cx - outerRadius; px <= cx + outerRadius; px++) {
      const long dx = px - cx;
      const long distanceSquared = dx * dx + dySquared;
      if (distanceSquared <= innerSquared || distanceSquared > outerSquared) continue;
      if (!full && !insideSweep(std::atan2(static_cast<float>(py - cy), static_cast<float>(px - cx)), startAngle,
                                sweep))
        continue;
      renderer.drawPixel(px, py, true);
    }
  }
}

void fillAnnulusToneSweep(const GfxRenderer& renderer, const int cx, const int cy, const int outerRadius,
                          const int innerRadius, const float startAngle, const float sweep,
                          const GfxRenderer::FillTone tone) {
  if (innerRadius >= outerRadius || sweep <= 0.0f) return;
  const long innerSquared = static_cast<long>(innerRadius) * innerRadius;
  const long outerSquared = static_cast<long>(outerRadius) * outerRadius;
  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  for (int py = std::max(0, cy - outerRadius); py <= std::min(screenHeight - 1, cy + outerRadius); py++) {
    for (int px = std::max(0, cx - outerRadius); px <= std::min(screenWidth - 1, cx + outerRadius); px++) {
      const long dx = px - cx;
      const long dy = py - cy;
      const long distanceSquared = dx * dx + dy * dy;
      if (distanceSquared <= innerSquared || distanceSquared > outerSquared) continue;
      if (!insideSweep(std::atan2(static_cast<float>(py - cy), static_cast<float>(px - cx)), startAngle, sweep))
        continue;
      renderer.rectangle.fill(px, py, 1, 1, static_cast<int>(tone), false);
    }
  }
}

void drawArcOutlinePixels(const GfxRenderer& renderer, const int cx, const int cy, const int radius,
                          const float startAngle, const float sweep) {
  if (radius < 1 || sweep <= 0.0f) return;
  const int samples = std::max(120, radius * 8);
  for (int i = 0; i <= samples; i++) {
    const float angle = startAngle + sweep * static_cast<float>(i) / static_cast<float>(samples);
    const int px = cx + static_cast<int>(radius * std::cos(angle) + 0.5f);
    const int py = cy + static_cast<int>(radius * std::sin(angle) + 0.5f);
    renderer.drawPixel(px, py, true);
  }
}

void drawReadingGauge(const GfxRenderer& renderer, const int cx, const int baseY, const int outerRadius,
                      const int thickness, const float progress) {
  const int usedThickness = std::max(6, std::min(outerRadius - 8, thickness));
  const int innerRadius = outerRadius - usedThickness;
  if (innerRadius <= 2) return;

  const int fillOuter = outerRadius - 1;
  const int fillInner = innerRadius + 1;
  constexpr float startAngle = kPi;
  constexpr float halfCircle = kPi;
  fillAnnulusToneSweep(renderer, cx, baseY, fillOuter, fillInner, startAngle, halfCircle,
                       GfxRenderer::FillTone::Gray);

  const float clampedProgress = std::min(1.0f, std::max(0.0f, progress));
  if (clampedProgress >= 0.999f) {
    fillAnnulusToneSweep(renderer, cx, baseY, fillOuter, fillInner, startAngle, halfCircle,
                         GfxRenderer::FillTone::Ink);
  } else if (clampedProgress > 0.004f) {
    fillAnnulusWedge(renderer, cx, baseY, fillOuter, fillInner, startAngle,
                     clampedProgress * halfCircle + 0.02f);
  }

  drawArcOutlinePixels(renderer, cx, baseY, outerRadius, startAngle, halfCircle);
  drawArcOutlinePixels(renderer, cx, baseY, innerRadius, startAngle, halfCircle);
}

std::string recentBookTitle() {
  if (RECENT_BOOKS.getBooks().empty()) return {};
  const RecentBook& book = RECENT_BOOKS.getBooks().front();
  if (!book.title.empty()) return book.title;
  const size_t slash = book.path.find_last_of('/');
  const size_t start = slash == std::string::npos ? 0 : slash + 1;
  const size_t dot = book.path.find_last_of('.');
  const size_t end = dot == std::string::npos || dot < start ? book.path.size() : dot;
  return book.path.substr(start, end - start);
}

int readingButtonHeight(const GfxRenderer& renderer) {
  const int actionLine = renderer.text.getLineHeight(MONTSERRAT_10_FONT_ID);
  const int subtitleLine = renderer.text.getLineHeight(MONTSERRAT_8_FONT_ID);
  constexpr int paddingY = 10;
  constexpr int lineGap = 1;
  return actionLine + lineGap + subtitleLine + paddingY * 2;
}

void drawReadingButton(const GfxRenderer& renderer, const int x, const int y, const int width, const int height,
                       const std::string& bookTitle) {
  const int actionFont = MONTSERRAT_10_FONT_ID;
  const int subtitleFont = MONTSERRAT_8_FONT_ID;
  constexpr int paddingY = 6;
  constexpr int lineGap = 1;
  const int radius = std::max(1, height / 2);
  if (width <= radius * 2) {
    renderer.circle.render(x + width / 2, y + height / 2, std::min(radius, width / 2), true);
  } else {
    renderer.rectangle.fill(x + radius, y, width - radius * 2, height, true, false);
    renderer.circle.render(x + radius, y + radius, radius, true);
    renderer.circle.render(x + width - radius - 1, y + radius, radius, true);
  }

  const char* action = bookTitle.empty() ? "Start Reading" : "Keep Reading";
  const int actionWidth = renderer.text.getWidth(actionFont, action, EpdFontFamily::BOLD);
  renderer.text.render(actionFont, x + (width - actionWidth) / 2, y + paddingY, action, false, EpdFontFamily::BOLD);

  const std::string subtitle = bookTitle.empty() ? "Open a book to begin" : bookTitle;
  const std::string truncated = renderer.text.truncate(subtitleFont, subtitle.c_str(), std::max(1, width - 24));
  const int textWidth = renderer.text.getWidth(subtitleFont, truncated.c_str());
  const int subtitleY = y + paddingY + renderer.text.getLineHeight(actionFont) + lineGap;
  renderer.text.render(subtitleFont, x + (width - textWidth) / 2, subtitleY, truncated.c_str(), false);
}

}  // namespace

void TodaysReading::render(const int x, const int y, const int width, const int height) const {
  renderContent(x, y, width, height, false);
}

void TodaysReading::preview(const int x, const int y, const int width, const int height) const {
  renderContent(x, y, width, height, true);
}

bool TodaysReading::buttonHitTest(const int tapX, const int tapY, const int areaX, const int areaY,
                                  const int areaWidth, const int areaHeight) const {
  if (areaWidth <= 0 || areaHeight <= 0) return false;
  const int designHeight = std::min(areaHeight, kDesignHeight);
  const int designY = areaY + (areaHeight - designHeight) / 2;
  const int buttonHeight = readingButtonHeight(renderer_);
  const int buttonY = designY + designHeight - buttonHeight - 5;
  const int buttonWidth = std::max(1, areaWidth * 40 / 100);
  const int buttonX = areaX + (areaWidth - buttonWidth) / 2;
  return tapX >= buttonX && tapX < buttonX + buttonWidth && tapY >= buttonY && tapY < buttonY + buttonHeight;
}

void TodaysReading::renderContent(const int x, const int y, const int width, const int height, const bool sample) const {
  if (width <= 0 || height <= 0) return;
  renderer_.rectangle.fill(x, y, width, height, false);

  ReadingGoal::Status status = ReadingGoal::status();
  if (sample) {
    status.rtcAvailable = true;
    status.readingTimeMs = 0;
    status.goalMinutes = 5;
  }
  if (!status.rtcAvailable) {
    renderer_.text.render(MONTSERRAT_8_FONT_ID, x + 12, y + height / 2 - 5, "Today's Reading unavailable", true,
                          EpdFontFamily::BOLD);
    return;
  }

  const int designHeight = std::min(height, kDesignHeight);
  const int designY = y + (height - designHeight) / 2;
  const int titleFont = MONTSERRAT_10_FONT_ID;
  const int goalFont = MONTSERRAT_8_FONT_ID;
  const int titleLine = renderer_.text.getLineHeight(titleFont);
  const int goalLine = renderer_.text.getLineHeight(goalFont);

  const int buttonHeight = readingButtonHeight(renderer_);
  const int buttonY = designY + designHeight - buttonHeight - 5;
  const int gaugeTop = designY + 8;
  const int gaugeBottomLimit = buttonY - 40;
  const int availableGaugeRadius = std::max(20, std::min(width / 2 - 12, gaugeBottomLimit - gaugeTop));
  const int gaugeRadius = std::max(20, availableGaugeRadius);
  const int gaugeBaseY = gaugeTop + availableGaugeRadius;
  const int gaugeCenterX = x + width / 2;

  const uint32_t elapsedSeconds = status.readingTimeMs / 1000;
  char duration[16];
  std::snprintf(duration, sizeof(duration), "%u:%02u", static_cast<unsigned>(elapsedSeconds / 60),
                static_cast<unsigned>(elapsedSeconds % 60));
  const float progress = status.goalMinutes > 0
                             ? std::min(1.0f, status.readingTimeMs / (static_cast<float>(status.goalMinutes) * 60000.0f))
                             : 0.0f;

  char goalText[64];
  if (status.goalMinutes > 0) {
    std::snprintf(goalText, sizeof(goalText), "of your %u-minute goal",
                  static_cast<unsigned>(status.goalMinutes));
  } else {
    std::snprintf(goalText, sizeof(goalText), "Set a reading goal in Reader settings  >");
  }
  const int timeFont = MONTSERRAT_18_FONT_ID;
  const int timeLine = renderer_.text.getLineHeight(timeFont);
  const int stackGap = 5;
  const int stackHeight = titleLine + stackGap + timeLine + stackGap + goalLine;
  const int stackTop = gaugeBaseY - stackHeight;
  const int titleY = stackTop;
  const int timeY = titleY + titleLine + stackGap;
  const int goalY = timeY + timeLine + stackGap;

  drawReadingGauge(renderer_, gaugeCenterX, gaugeBaseY, gaugeRadius, std::max(4, gaugeRadius / 12), progress);

  const int titleWidth = renderer_.text.getWidth(titleFont, "Today's Reading", EpdFontFamily::BOLD);
  renderer_.text.render(titleFont, x + std::max(0, (width - titleWidth) / 2), titleY, "Today's Reading", true,
                        EpdFontFamily::BOLD);
  const int durationWidth = renderer_.text.getWidth(timeFont, duration);
  renderer_.text.render(timeFont, x + std::max(0, (width - durationWidth) / 2), timeY, duration, true);
  const std::string goalDisplay = renderer_.text.truncate(goalFont, goalText, std::max(1, width - 24));
  const int goalWidth = renderer_.text.getWidth(goalFont, goalDisplay.c_str());
  renderer_.text.render(goalFont, x + std::max(0, (width - goalWidth) / 2), goalY, goalDisplay.c_str(), true);

  const std::string bookTitle = sample ? std::string{} : recentBookTitle();
  const int buttonWidth = std::max(1, width * 60 / 100);
  const int buttonX = x + (width - buttonWidth) / 2;
  drawReadingButton(renderer_, buttonX, buttonY, buttonWidth, buttonHeight, bookTitle);

  renderedKey_ = static_cast<uint64_t>(status.dateKey) * 100000ULL +
                 static_cast<uint64_t>(status.readingTimeMs / 60000ULL) * 10ULL + status.goalMinutes;
}

bool TodaysReading::needsRefresh() const {
  const ReadingGoal::Status status = ReadingGoal::status();
  if (!status.rtcAvailable) return false;
  const uint64_t key = static_cast<uint64_t>(status.dateKey) * 100000ULL +
                       static_cast<uint64_t>(status.readingTimeMs / 60000ULL) * 10ULL + status.goalMinutes;
  return key != renderedKey_;
}
