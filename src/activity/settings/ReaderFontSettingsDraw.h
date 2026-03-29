#pragma once

#include <GfxRenderer.h>

#include <cstdint>

namespace ReaderFontSettingsDraw {

/** Right edge of the value column (e.g. screenWidth - 24). */
void drawFontFamilyRowValue(const GfxRenderer& renderer, uint8_t fontFamily, int valueColumnRight, int itemY,
                            int itemHeight, bool rowSelected, const char* familyLabel);

/** Compatibility wrapper for the shared Apple-style Toggle component. */
void drawToggleCheckbox(const GfxRenderer& renderer, int valueColumnRight, int itemY, int itemHeight, bool rowSelected,
                        bool checked);

}  // namespace ReaderFontSettingsDraw
