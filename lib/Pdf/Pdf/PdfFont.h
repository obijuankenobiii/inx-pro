/**
 * @file PdfFont.h
 * @brief Font resolution: maps a PDF font resource dict to one of the firmware's built-in bitmap fonts
 * (ChareInk). We never render a PDF's own glyph outlines (embedded or not) - only its own
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
  bool isCID = false;
  EpdFontFamily::Style style = EpdFontFamily::REGULAR;

  int firstChar = 0;
  std::vector<int> widths;
  std::array<uint32_t, 256> codeToUnicode{};
  int defaultWidth = 500;

  std::map<uint32_t, uint32_t> cidToUnicode;
  std::map<uint32_t, int> cidWidths;
  int cidDefaultWidth = 1000;
};

class PdfFont {
 public:
  static PdfFontInfo resolve(const PdfDocument& doc, const PdfObject& fontDict);

  static int widthForCode(const PdfFontInfo& font, uint8_t code);

  static int widthForCid(const PdfFontInfo& font, uint32_t cid);

  static uint32_t unicodeForCid(const PdfFontInfo& font, uint32_t cid);

  static int nearestBuiltinFontId(double devicePixelSize);

  static void appendUtf8(std::string& out, uint32_t codepoint);
};
