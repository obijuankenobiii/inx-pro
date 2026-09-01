/**
 * @file ImageToneMap.cpp
 * @brief Xteink X4 Pro image tone mapping.
 *
 * The four 2-bit plane codes do not land in the same brightness order on this panel as on
 * Sticky, so the image tones are mapped differently. This is the right layer for that: it
 * changes which code an image tone is drawn with, and leaves the waveform and the driver's
 * refresh bookkeeping completely alone.
 */

#include <cstdint>

#include <BoardConfig.h>

// UC8279 plane codes per tone. Text and images need DIFFERENT mappings, which is why
// grayscaleCodeTable() and mediumTextCodeTable() exist as separate entry points.
//
// TEXT AA: stock FreeInk mapping, left exactly as it was found. It pairs with the stock AA
// LUT (X4PRO_TEXT_AA_STOCK below), where registers 0x22 and 0x23 are identical - glyph AA
// only needs white, one mid grey and black, so white and black sharing 0b00 is correct
// here, not a defect. Do not "fix" it; the four-tone work belongs to images.
#ifndef X4PRO_TONE_WHITE
#define X4PRO_TONE_WHITE 0b00
#endif
#ifndef X4PRO_TONE_DARK
#define X4PRO_TONE_DARK 0b11
#endif
#ifndef X4PRO_TONE_LIGHT
#define X4PRO_TONE_LIGHT 0b10
#endif
#ifndef X4PRO_TONE_BLACK
#define X4PRO_TONE_BLACK 0b00
#endif

// Images: need four SEPARATED tones, and the polarity comes out flipped against text.
#ifndef X4PRO_IMAGE_TONE_WHITE
#define X4PRO_IMAGE_TONE_WHITE 0b11
#endif
#ifndef X4PRO_IMAGE_TONE_DARK
#define X4PRO_IMAGE_TONE_DARK 0b01
#endif
#ifndef X4PRO_IMAGE_TONE_LIGHT
#define X4PRO_IMAGE_TONE_LIGHT 0b10
#endif
#ifndef X4PRO_IMAGE_TONE_BLACK
#define X4PRO_IMAGE_TONE_BLACK 0b00
#endif

const uint8_t* grayscaleCodeTable() {
  if (BoardConfig::ACTIVE.displayController == BoardConfig::DisplayController::UC8279) {
    // Codes are the (old, new) plane pair selecting a transition LUT register: 0x21 WW,
    // 0x22 BW, 0x23 WB, 0x24 BB. Endpoints are confirmed on hardware (white 0b11, black
    // 0b00); the two midtones are the part that keeps coming out wrong, so they are build
    // flags - override in platformio.ini rather than editing here.
    //
    // NOTE the index order is the FourToneImageDitherer level order, which is NOT in
    // brightness order: 0 white, 1 DARK grey, 2 LIGHT grey, 3 black.
    static constexpr uint8_t kUc8279[4] = {
        X4PRO_IMAGE_TONE_WHITE,
        X4PRO_IMAGE_TONE_DARK,
        X4PRO_IMAGE_TONE_LIGHT,
        X4PRO_IMAGE_TONE_BLACK,
    };
    return kUc8279;
  }
  static constexpr uint8_t kUc8179[4] = {
      0b11,
      0b01,
      0b10,
      0b00,
  };
  return kUc8179;
}

const uint8_t* mediumTextCodeTable() {
  if (BoardConfig::ACTIVE.displayController == BoardConfig::DisplayController::UC8279) {
    static constexpr uint8_t kUc8279Text[4] = {
        X4PRO_TONE_WHITE,
        X4PRO_TONE_DARK,
        X4PRO_TONE_LIGHT,
        X4PRO_TONE_BLACK,
    };
    return kUc8279Text;
  }
  return grayscaleCodeTable();
}

uint8_t mapQualityGray2Level(const uint8_t level) {
  const uint8_t l = level & 3u;
  if (BoardConfig::ACTIVE.displayController == BoardConfig::DisplayController::UC8179) {
    if (l == 0u) return 2u;
    if (l == 1u) return 0u;
    if (l == 2u) return 1u;
    return l;
  }
  if (l == 1u) return 2u;
  if (l == 2u) return 1u;
  return l;
}

#ifndef X4PRO_IMAGE_CONTRAST
#define X4PRO_IMAGE_CONTRAST 145
#endif
#ifndef X4PRO_IMAGE_DARKEN
#define X4PRO_IMAGE_DARKEN 12
#endif

int applyDeviceToneCurve(const int gray) {
  int g = ((gray - 128) * X4PRO_IMAGE_CONTRAST) / 100 + 128;
  g -= X4PRO_IMAGE_DARKEN;
  if (g < 0) g = 0;
  if (g > 255) g = 255;
  return g;
}
