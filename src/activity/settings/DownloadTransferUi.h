#pragma once

#include <GfxRenderer.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <string>

#include "activity/page/components/widget/BaseCarousel.h"
#include "state/SystemSetting.h"
#if FREEINK_DEVICE_X4PRO
#include "images/X4ProWifiTransferSetup.h"
#elif FREEINK_DEVICE_STICKY || FREEINK_DEVICE_MURPHY_M4
#include "images/MurphyStickyWifiTransferSetup.h"
#else
#include "images/WifiTransferSetup.h"
#endif

namespace DownloadTransferUi {

namespace {
constexpr int kImageWidth = 400;
constexpr int kImageHeight = 200;
constexpr int kImageGap = 15;
constexpr int kCardPadding = 20;
constexpr int kPanelButtonGap = 20;
constexpr int kTextGap = 20;

#if FREEINK_DEVICE_X4PRO
const uint8_t* const kImage = X4ProWifiTransferSetup;
#elif FREEINK_DEVICE_STICKY || FREEINK_DEVICE_MURPHY_M4
const uint8_t* const kImage = MurphyStickyWifiTransferSetup;
#else
const uint8_t* const kImage = WifiTransferSetup;
#endif

inline void progressBar(const GfxRenderer& renderer, const int x, const int y, const int width, const int height,
                        const int percent) {
  renderer.rectangle.render(x, y, width, height, true);
  const int innerWidth = std::max(1, width - 2);
  renderer.rectangle.fill(x + 1, y + 1, innerWidth, height - 2, false);
  const int fillWidth = std::max(0, std::min(innerWidth, innerWidth * percent / 100));
  if (fillWidth > 0) renderer.rectangle.fill(x + 1, y + 1, fillWidth, height - 2, true);
}
}  // namespace

inline void render(GfxRenderer& renderer, const int bodyTop, const int font, const char* title,
                   const std::string& packageName, const size_t downloaded, const size_t total,
                   const bool installing) {
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const int percent = total > 0 ? std::max(0, std::min(100, static_cast<int>((downloaded * 100) / total))) : 0;
  const int imageX = (screenW - kImageWidth) / 2;
  const int imageY = bodyTop;
  renderer.bitmap.icon(kImage, imageX, imageY, kImageWidth, kImageHeight);
  const int panelY = imageY + kImageHeight + kImageGap;
  const int panelBottom = std::max(panelY + 1, screenH - kPanelButtonGap);
  for (int py = panelY; py < panelBottom; py += 2) {
    const int offset = ((py - panelY) & 2) != 0 ? 0 : 1;
    for (int px = offset; px < screenW; px += 2) renderer.drawPixel(px, py, true);
  }

  const int cardX = kCardPadding;
  const int cardY = panelY + 10;
  const int cardWidth = screenW - 2 * kCardPadding;
  const int lineHeight = renderer.text.getLineHeight(font);
  const int headerY = cardY + kCardPadding;
  const int nameY = headerY + lineHeight + kTextGap;
  const int firstRowY = nameY + lineHeight + kTextGap;
  const int barY = firstRowY + lineHeight + 2;
  const int secondRowY = barY + 14;
  const int cardHeight = std::min(panelBottom - cardY, secondRowY - cardY + lineHeight + kCardPadding);
  const bool rounded = SETTINGS.bitmapRoundedCorners != 0;
  BaseCarousel::renderShadow(renderer, cardX + 4, cardY + 4, cardWidth, cardHeight,
                             HomeTheme::CarouselShadowStyle::Gray);
  renderer.rectangle.fill(cardX, cardY, cardWidth, cardHeight, false, rounded);
  renderer.rectangle.render(cardX, cardY, cardWidth, cardHeight, true, rounded);
  const int contentX = cardX + kCardPadding;
  renderer.text.render(font, contentX, headerY, title, true, EpdFontFamily::BOLD);
  const std::string displayName = renderer.text.truncate(font, packageName.c_str(), cardWidth - 2 * kCardPadding);
  renderer.text.render(font, contentX, nameY, displayName.c_str(), true);
  renderer.text.render(font, contentX, firstRowY, "Download", true);
  char percentText[8];
  std::snprintf(percentText, sizeof(percentText), "%d%%", percent);
  const int percentWidth = renderer.text.getWidth(font, percentText);
  renderer.text.render(font, cardX + cardWidth - kCardPadding - percentWidth, firstRowY, percentText, true);
  progressBar(renderer, contentX, barY, cardWidth - 2 * kCardPadding, 6, percent);
  const char* status = installing ? "Installing" : "Waiting";
  renderer.text.render(font, cardX + cardWidth - kCardPadding - renderer.text.getWidth(font, status), secondRowY,
                       status, true);
}

}  // namespace DownloadTransferUi
