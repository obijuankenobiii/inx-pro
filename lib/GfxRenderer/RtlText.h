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

/**
 * Returns text in visual order when it contains RTL text. Arabic letters are
 * converted to contextual presentation forms because the embedded rasterizer
 * does not run OpenType shaping tables.
 *
 * The returned pointer is either the input string (no RTL present) or
 * outputStorage.c_str(). outputStorage must remain alive while the pointer is
 * used.
 */
const char* prepareForRender(const char* text, std::string& outputStorage);

}  // namespace RtlText
