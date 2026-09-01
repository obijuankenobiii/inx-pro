/**
 * @file ReaderFontSettingsDraw.cpp
 * @brief Shared reader font preview drawing (system settings + book settings drawer).
 */

#include "ReaderFontSettingsDraw.h"

#include <EpdFontFamily.h>

#include "activity/page/components/global/Toggle.h"
#include "system/Fonts.h"

namespace ReaderFontSettingsDraw {

void drawFontFamilyRowValue(const GfxRenderer& renderer, uint8_t fontFamily, int valueColumnRight, int itemY,
                            int itemHeight, bool rowSelected, const char* familyLabel) {
  (void)fontFamily;
  if (!familyLabel || familyLabel[0] == '\0') {
    return;
  }
  constexpr int previewFont = MONTSERRAT_8_FONT_ID;
  const bool black = !rowSelected;
  const int valW = renderer.text.getWidth(previewFont, familyLabel, EpdFontFamily::REGULAR);
  const int lh = renderer.text.getLineHeight(previewFont);
  const int valY = itemY + (itemHeight - lh) / 2;
  const int valX = valueColumnRight - valW;
  renderer.text.render(previewFont, valX, valY, familyLabel, black, EpdFontFamily::REGULAR);
}

void drawToggleCheckbox(const GfxRenderer& renderer, int valueColumnRight, int itemY, int itemHeight, bool rowSelected,
                        bool checked) {
  Toggle::render(renderer, valueColumnRight, itemY, itemHeight, checked, rowSelected);
}

}
