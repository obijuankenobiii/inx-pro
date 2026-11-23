/**
 * @file EpdFontData.h
 * @brief Public interface and types for EpdFontData.
 */

#pragma once
#include <cstdint>


typedef struct {
  uint8_t width;
  uint8_t height;
  uint8_t advanceX;
  int16_t left;
  int16_t top;
  uint16_t dataLength;
  uint32_t dataOffset;
} EpdGlyph;


typedef struct {
  uint32_t first;
  uint32_t last;
  uint32_t offset;
} EpdUnicodeInterval;


typedef struct {
  const uint8_t* bitmap;
  uint32_t bitmapSize;
  const EpdGlyph* glyph;
  const EpdUnicodeInterval* intervals;
  uint32_t intervalCount;
  uint8_t advanceY;
  int ascender;
  int descender;
  bool is2Bit;
} EpdFontData;
