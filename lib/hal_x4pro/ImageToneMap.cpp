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

// MEDIUM path (text AA + medium images). The code IS the two plane bits, written
// non-inverted: bit0 -> LSB pass -> DTM1/plane0, bit1 -> MSB pass -> DTM2/plane1.
// Uc8279X4Driver::copyGrayscale{Lsb,Msb} stream them straight through with invert=false and
// the waveform is the driver's built-in xtfAa set (selectAaLuts), so this table is the only
// knob the board owns on the medium path.
//
// Bucket order established ON HARDWARE, not from the driver comment - that comment says
// dark grey is (1,1) and light grey (0,1), which does NOT match this unit. Two observations
// pinned it, taken with the table reading {00, 10, 11, 01}:
//     level 2 light grey sent 0b11 -> came out BLACK       => 0b11 = black
//     level 3 black      sent 0b01 -> came out DARK GREY   => 0b01 = dark grey
// White was correct at 0b00, leaving 0b10 = light grey by elimination. The panel order is
// therefore monotonic - 00 white, 01 dark grey, 10 light grey, 11 black - so the correct
// table is the identity, code == level.
//
// Renderer levels are fixed by FourToneImageDitherer::levelFromValue (brightness <=36 -> 3,
// <=166 -> 1, <=236 -> 2, else 0): 0 white, 1 dark grey, 2 light grey, 3 black.
const uint8_t* grayscaleCodeTable() {
  if (BoardConfig::ACTIVE.displayController == BoardConfig::DisplayController::UC8279) {
    // Established on hardware - see the note above this function.
    static constexpr uint8_t kUc8279[4] = {
        0b00,  // level 0  white
        0b01,  // level 1  dark gray
        0b10,  // level 2  light gray
        0b11,  // level 3  black
    };
    return kUc8279;
  }
  // UC8179. Confirmed good on hardware. Uc8179Driver::copyGrayscaleLsb documents the
  // selectors as (plane0/DTM1, plane1/DTM2) black=(0,0) dark=(1,0) light=(0,1) white=(1,1),
  // so white=0b11 and black=0b00 - the EXTREMES ARE INVERTED versus UC8279 above, midtones
  // the same. The two controllers do not share a plane-code order; sharing one table sent
  // this panel's solid black to 0b11 and washed text out to grey.
  static constexpr uint8_t kUc8179[4] = {
      0b11,  // level 0  white  (1,1)
      0b01,  // level 1  dark   (1,0)
      0b10,  // level 2  light  (0,1)
      0b00,  // level 3  black  (0,0)
  };
  return kUc8179;
}

// Text AA plane codes. UC8279 uses the image table - unifying them is what made its text
// correct. UC8179 keeps the shipped overlay codes, which hold both extremes on the B/W base
// (0b00) and drive only the two edge levels.
const uint8_t* mediumTextCodeTable() {
  // Text AA is the same 2-bit data on the same levels through the same planes, so it uses
  // the image table on both controllers. Unifying them is what made UC8279's text correct,
  // and UC8179 is confirmed good the same way. A separate hand-rolled text table is what
  // previously left dark edges pinned to solid black.
  return grayscaleCodeTable();
}

// QUALITY (GRAY2) path. UC8179's wrapper swaps the two physical mid-tone LUT
// registers, so image levels white/dark/light/black select 3/2/1/0. The other
// X4 Pro controller keeps the established middle-tone swap.
uint8_t mapQualityGray2Level(const uint8_t level) {
  const uint8_t l = level & 3u;
  if (BoardConfig::ACTIVE.displayController == BoardConfig::DisplayController::UC8179) {
    if (l == 0u) return 2u;  // white      -> light grey's code
    if (l == 1u) return 0u;  // dark grey  -> white's original code
    if (l == 2u) return 1u;  // light grey -> white's current code
    return l;                // black unchanged
  }
  if (l == 1u) return 2u;
  if (l == 2u) return 1u;
  return l;
}

// --- Tone curve -----------------------------------------------------------------------
// The X4 Pro renders images noticeably flatter than Sticky: the whole range is compressed
// toward white and the dark end never arrives (side-by-side photo, same cover). The panel
// reaches those tones — the OEM firmware shows it — so this is a tone-mapping problem, not
// a waveform one, and correcting it here costs nothing at refresh time.
//
// Applied to the 8-bit grey value BEFORE 2-bit quantization, so it changes which of the
// four levels a pixel lands on rather than how any level is driven. Two stages:
//   contrast : expand around mid-grey, so mid-tones separate instead of bunching
//   darken   : shift the result down, so the dark end actually reaches level 3
//
// Both are build-tunable — sweep them without editing this file:
//   -DX4PRO_IMAGE_CONTRAST=145   (percent; 100 = off)
//   -DX4PRO_IMAGE_DARKEN=12      (8-bit levels subtracted after contrast; 0 = off)
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
