#include "ImageWidget.h"

#include <GfxRenderer.h>
#include <ImageRender.h>

#include <algorithm>

#include "state/SystemSetting.h"
#include "system/Fonts.h"

void ImageWidget::render(const int x, const int y, const int width, const int height, const char* path,
                         const bool cropToFill, const bool padding, const float cropAnchorX,
                         const float cropAnchorY) const {
  if (width <= 0 || height <= 0) return;

  const bool rounded = SETTINGS.bitmapRoundedCorners != 0;
  const bool subtleRounded = SETTINGS.bitmapRoundedCorners == 2;
  renderer_.rectangle.fill(x, y, width, height, false, rounded, subtleRounded);
  const int inset = padding ? 20 : 0;
  const int imageX = x + std::min(inset, width / 2);
  const int imageY = y + std::min(inset, height / 2);
  const int imageWidth = std::max(1, width - std::min(inset, width / 2) * 2);
  const int imageHeight = std::max(1, height - std::min(inset, height / 2) * 2);
  if (path != nullptr && path[0] != '\0') {
    ImageRender::Options options;
    options.mode = ImageRenderMode::OneBit;
    options.cropToFill = cropToFill;
    options.cropAnchorX = cropAnchorX;
    options.cropAnchorY = cropAnchorY;
    if (SETTINGS.bitmapRoundedCorners == 1) {
      options.roundedOutside = BitmapRender::RoundedOutside::PaperOutside;
    } else if (SETTINGS.bitmapRoundedCorners == 2) {
      options.roundedOutside = BitmapRender::RoundedOutside::SubtlePaperOutside;
    }
    options.useDisplayCache = true;
    options.asyncDisplayCache = true;
    options.cacheNamespace = ImageDisplayCacheNamespace::Widgets;
    if (ImageRender::create(renderer_, path).render(imageX, imageY, imageWidth, imageHeight, options)) return;
  }

  renderer_.rectangle.render(imageX, imageY, imageWidth, imageHeight, true, rounded, subtleRounded);
  const int font = systemFontIdForPointSize(12);
  const char* label = path != nullptr && path[0] != '\0' ? "Image unavailable" : "Choose image";
  const int labelWidth = renderer_.text.getWidth(font, label);
  const int lineHeight = renderer_.text.getLineHeight(font);
  renderer_.text.render(font, imageX + std::max(0, (imageWidth - labelWidth) / 2),
                        imageY + std::max(0, (imageHeight - lineHeight) / 2), label, true,
                        EpdFontFamily::REGULAR);
}
