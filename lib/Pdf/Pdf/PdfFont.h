/**
 * @file PdfFont.h
 * @brief Font resolution: maps a PDF font resource dict to one of the firmware's built-in bitmap fonts
 * (Atkinson Hyperlegible). We never render a PDF's own glyph outlines (embedded or not) - only its own
 * text/encoding metadata (BaseFont style hints, /Widths or /W, /Encoding, /ToUnicode) is used to pick the
 * right characters and advance widths, then the system font draws them. This means embedding is irrelevant
 * to whether a font is supported: a font with /FontFile2 renders exactly like one without.
 *
 * Simple fonts (Type1/TrueType/MMType1, one byte per character code) and Type0 composite fonts using
 * Identity-H/V encoding (two bytes per code, the overwhelmingly common case for embedded CID fonts) are
 * both supported. Other multi-byte encodings and Type3 (procedural) fonts are not.
 */

#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <EpdFontFamily.h>

class PdfDocument;
class PdfObject;

struct PdfFontInfo {
  bool supported = false;
  bool isCID = false;  // true: 2-byte codes (Type0/Identity-H|V); false: 1-byte simple font
  EpdFontFamily::Style style = EpdFontFamily::REGULAR;

  // Simple fonts:
  int firstChar = 0;
  std::vector<int> widths;                    // glyph-space units (1000 = 1 em), indexed by (code - firstChar)
  std::array<uint32_t, 256> codeToUnicode{};   // WinAnsiEncoding by default; 0 = not renderable
  int defaultWidth = 500;

  // CID fonts (Identity-H/V only):
  std::map<uint32_t, uint32_t> cidToUnicode;  // from /ToUnicode; a missing entry falls back to code == cid
  std::map<uint32_t, int> cidWidths;          // from /DescendantFonts[0]/W
  int cidDefaultWidth = 1000;                 // from /DescendantFonts[0]/DW
};

class PdfFont {
 public:
  // Resolves a /Font resource dictionary (already dereferenced) into a PdfFontInfo.
  static PdfFontInfo resolve(const PdfDocument& doc, const PdfObject& fontDict);

  // Glyph-space (1000 units/em) advance width for one WinAnsi-encoded byte (simple fonts).
  static int widthForCode(const PdfFontInfo& font, uint8_t code);

  // Glyph-space advance width for one CID (CID fonts).
  static int widthForCid(const PdfFontInfo& font, uint32_t cid);

  // Unicode codepoint for one CID, via /ToUnicode if present, else identity (cid treated as the codepoint).
  static uint32_t unicodeForCid(const PdfFontInfo& font, uint32_t cid);

  // Picks the built-in bitmap font id whose fixed size is closest to devicePixelSize.
  static int nearestBuiltinFontId(double devicePixelSize);

  static void appendUtf8(std::string& out, uint32_t codepoint);
};
