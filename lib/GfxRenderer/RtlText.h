#pragma once

#include <cstdint>
#include <string>

namespace RtlText {

enum class Direction : uint8_t {
  NONE = 0,
  LTR,
  RTL,
};

/** Returns the first strong direction in a UTF-8 string. */
Direction firstStrongDirection(const char* text);

/** Returns true for RTL-script non-spacing marks (Arabic harakat/Quranic
 * annotations and Hebrew niqqud/cantillation). These glyphs are overlays,
 * not advances in the text cursor. */
bool isTransparentMark(uint32_t codepoint);

using GlyphCoverage = bool (*)(uint32_t codepoint, void* context);

/**
 * Returns a renderer-safe UTF-8 string. Soft hyphens are removed and Unicode
 * presentation ligatures are expanded to their constituent letters so font
 * fallback and bold-prefix rendering do not depend on a ligature glyph.
 */
std::string normalizeForLayout(const std::string& text);

/**
 * Splits a logical LTR word for Bionic Reading. The split is calculated after
 * UTF-8 normalization and therefore always lands on a codepoint boundary.
 * A false enabled value, or a short word, returns the complete word in suffix.
 */
void splitBionicWord(const std::string& text, bool enabled, std::string& prefix, std::string& suffix);

/**
 * Returns text in visual order when it contains RTL text. Arabic letters are
 * converted to contextual presentation forms because the embedded rasterizer
 * does not run OpenType shaping tables.
 *
 * The returned pointer is either the input string (no RTL present) or
 * outputStorage.c_str(). outputStorage must remain alive while the pointer is
 * used.
 */
const char* prepareForRender(const char* text, std::string& outputStorage,
                             GlyphCoverage coverage = nullptr, void* coverageContext = nullptr);

}  // namespace RtlText
