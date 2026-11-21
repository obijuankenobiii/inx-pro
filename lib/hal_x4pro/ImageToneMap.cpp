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








#ifndef X4PRO_TONE_WHITE
#define X4PRO_TONE_WHITE 0b00
#endif
#ifndef X4PRO_TONE_DARK
#define X4PRO_TONE_DARK 0b00
#endif
#ifndef X4PRO_TONE_LIGHT
#define X4PRO_TONE_LIGHT 0b10
#endif
#ifndef X4PRO_TONE_BLACK
#define X4PRO_TONE_BLACK 0b00
#endif


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
#define X4PRO_IMAGE_CONTRAST 100
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
