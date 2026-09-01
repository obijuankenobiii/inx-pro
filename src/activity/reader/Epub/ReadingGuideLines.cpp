#include "ReadingGuideLines.h"

#include <Epub/Page.h>
#include <GfxRenderer.h>

namespace ReadingGuideLines {

void render(GfxRenderer& renderer, const Page& page, const int mode, const int orientedMarginTop,
            const int orientedMarginRight, const int orientedMarginBottom, const int orientedMarginLeft,
            const int bodyFontId) {
  if (mode == 0) {
    return;
  }

  const int contentWidth = renderer.getScreenWidth() - orientedMarginLeft - orientedMarginRight;
  if (contentWidth < 3) {
    return;
  }
  const int lineTop = orientedMarginTop;
  const int lineBottom = renderer.getScreenHeight() - orientedMarginBottom;
  if (lineBottom <= lineTop) {
    return;
  }

  if (mode == 2) {
    constexpr int kClearancePx = 4;
    const int contentRight = renderer.getScreenWidth() - orientedMarginRight;
    for (const auto& element : page.elements) {
      int lineFontId = bodyFontId;
      switch (element->getTag()) {
        case TAG_PageLine:
          break;
        case TAG_PageHeader:
          lineFontId = static_cast<const PageHeader&>(*element).getHeaderFontId();
          break;
        case TAG_PageSmallCaps:
          lineFontId = static_cast<const PageSmallCaps&>(*element).getCompatFontId();
          break;
        default:
          continue;
      }
      const int ascender = renderer.text.getFontAscenderSize(lineFontId);
      const int y = orientedMarginTop + element->yPos + ascender + kClearancePx + 2;
      if (y >= lineTop && y < lineBottom) {
        renderer.line.render(orientedMarginLeft, y, contentRight, y, true, LineRender::Style::Dotted);
      }
    }
    return;
  }

  const int x1 = orientedMarginLeft + contentWidth / 3;
  const int x2 = orientedMarginLeft + (contentWidth * 2) / 3;
  renderer.line.render(x1, lineTop, x1, lineBottom, true, LineRender::Style::Dotted);
  renderer.line.render(x2, lineTop, x2, lineBottom, true, LineRender::Style::Dotted);
}

}
