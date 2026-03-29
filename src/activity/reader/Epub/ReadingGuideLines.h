#pragma once

class GfxRenderer;
class Page;

/** Reader-page overlays that guide eye movement without changing EPUB layout. */
namespace ReadingGuideLines {

void render(GfxRenderer& renderer, const Page& page, int mode, int orientedMarginTop, int orientedMarginRight,
            int orientedMarginBottom, int orientedMarginLeft, int bodyFontId);

}  // namespace ReadingGuideLines
