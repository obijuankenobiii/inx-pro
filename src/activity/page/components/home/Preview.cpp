#include "Preview.h"

#include <Epub/Page.h>
#include <Epub/Section.h>
#include <GfxRenderer.h>

#include <algorithm>
#include <memory>

#include "state/BookSetting.h"
#include "system/Fonts.h"

namespace home {

bool preview(GfxRenderer& renderer, const std::string& cachePath, const int spine, const int page) {
  int font = MONTSERRAT_12_FONT_ID;
  Section section(cachePath, spine, renderer);
  if (!section.loadSectionFileForPreview(&font)) {
    return false;
  }
  section.currentPage = page;
  if (!FontManager::ensureReaderLayoutFonts(font, renderer)) {
    return false;
  }

  Page* cached = section.loadPageFromSectionFile();
  if (!cached) {
    return false;
  }

  BookSettings settings;
  settings.loadFromFile(cachePath);
  int top = 0;
  int right = 0;
  int bottom = 0;
  int left = 0;
  renderer.getOrientedViewableTRBL(&top, &right, &bottom, &left);
  (void)right;
  (void)bottom;

  const int x = left + settings.screenMargin;
  const int y = std::max(top, top + settings.screenMargin -
                                   renderer.text.getGlyphTopInset(font, 'H', EpdFontFamily::REGULAR));
  cached->render(renderer, font, FontManager::getNextFont(font), x, y, false, ImageRenderMode::OneBit);
  return true;
}

}  // namespace home
