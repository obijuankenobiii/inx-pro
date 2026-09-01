
#include "toojpeg.h"

#include <cstdlib>

namespace
{
using uint8_t = unsigned char;
using uint16_t = unsigned short;
using int16_t = short;
using int32_t = int;

const uint8_t DefaultQuantLuminance[8 * 8] = {
    16, 11,  10,  16, 24, 40, 51, 61,
    12, 12,  14,  19, 26, 58, 60, 55,
    14, 13,  16,  24, 40, 57, 69, 56,
    14, 17,  22,  29, 51, 87, 80, 62, 18,  22,  37,  56,  68, 109, 103, 77, 24,  35,  55,  64,
    81, 104, 113, 92, 49, 64, 78, 87, 103, 121, 120, 101, 72, 92,  95,  98, 112, 100, 103, 99};
const uint8_t DefaultQuantChrominance[8 * 8] = {17, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66, 99, 99, 99, 99,
                                                24, 26, 56, 99, 99, 99, 99, 99, 47, 66, 99, 99, 99, 99, 99, 99,
                                                99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
                                                99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99};

const uint8_t ZigZagInv[8 * 8] = {0,  1,  8,  16, 9,  2,  3,  10,
                                  17, 24, 32, 25, 18, 11, 4,  5,
                                  12, 19, 26, 33, 40, 48, 41, 34,
                                  27, 20, 13, 6,  7,  14, 21, 28,
                                  35, 42, 49, 56, 57, 50, 43, 36,
                                  29, 22, 15, 23, 30, 37, 44, 51,
                                  58, 59, 52, 45, 38, 31, 39, 46,
                                  53, 60, 61, 54, 47, 55, 62, 63};

const uint8_t DcLuminanceCodesPerBitsize[16] = {0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0};
const uint8_t DcLuminanceValues[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
const uint8_t AcLuminanceCodesPerBitsize[16] = {0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 125};
const uint8_t AcLuminanceValues[162] =
    {0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07, 0x22, 0x71, 0x14,
     0x32, 0x81, 0x91, 0xA1, 0x08,
     0x23, 0x42, 0xB1, 0xC1, 0x15, 0x52, 0xD1, 0xF0, 0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0A, 0x16, 0x17, 0x18, 0x19,
     0x1A, 0x25, 0x26, 0x27, 0x28,
     0x29, 0x2A, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x53, 0x54,
     0x55, 0x56, 0x57, 0x58, 0x59,
     0x5A, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x83, 0x84,
     0x85, 0x86, 0x87, 0x88, 0x89,
     0x8A, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA,
     0xB2, 0xB3, 0xB4, 0xB5, 0xB6,
     0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7,
     0xD8, 0xD9, 0xDA, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6,
     0xF7, 0xF8, 0xF9, 0xFA};
const uint8_t DcChrominanceCodesPerBitsize[16] = {0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0};
const uint8_t DcChrominanceValues[12] = {0, 1, 2, 3, 4,  5,
                                         6, 7, 8, 9, 10, 11};
const uint8_t AcChrominanceCodesPerBitsize[16] = {0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 119};
const uint8_t AcChrominanceValues[162] =
    {0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71, 0x13, 0x22, 0x32,
     0x81, 0x08, 0x14, 0x42, 0x91,
     0xA1, 0xB1, 0xC1, 0x09, 0x23, 0x33, 0x52, 0xF0, 0x15, 0x62, 0x72, 0xD1, 0x0A, 0x16, 0x24, 0x34, 0xE1, 0x25, 0xF1,
     0x17, 0x18, 0x19, 0x1A, 0x26,
     0x27, 0x28, 0x29, 0x2A, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x53,
     0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x73, 0x74, 0x75, 0x76,
     0x77, 0x78, 0x79, 0x7A, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
     0x98, 0x99, 0x9A, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8,
     0xB9, 0xBA, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9,
     0xDA, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA};
const int16_t CodeWordLimit = 2048;

struct BitCode {
  BitCode() = default;
  BitCode(uint16_t code_, uint8_t numBits_) : code(code_), numBits(numBits_) {}
  uint16_t code;
  uint8_t numBits;
};

struct BitWriter {
  TooJpeg::WRITE_ONE_BYTE output;
  explicit BitWriter(TooJpeg::WRITE_ONE_BYTE output_) : output(output_) {}

  struct BitBuffer {
    int32_t data = 0;
    uint8_t numBits = 0;
  } buffer;

  BitWriter& operator<<(const BitCode& data) {
    buffer.numBits += data.numBits;
    buffer.data <<= data.numBits;
    buffer.data |= data.code;

    while (buffer.numBits >= 8) {
      buffer.numBits -= 8;
      auto oneByte = uint8_t(buffer.data >> buffer.numBits);
      output(oneByte);

      if (oneByte == 0xFF)
        output(0);

    }
    return *this;
  }

  void flush() {
    *this << BitCode(0x7F, 7);
  }

  BitWriter& operator<<(uint8_t oneByte) {
    output(oneByte);
    return *this;
  }

  template <typename T, int Size>
  BitWriter& operator<<(T (&manyBytes)[Size]) {
    for (auto c : manyBytes) output(c);
    return *this;
  }

  void addMarker(uint8_t id, uint16_t length) {
    output(0xFF);
    output(id);
    output(uint8_t(length >> 8));
    output(uint8_t(length & 0xFF));
  }
};

template <typename Number>
Number minimum(Number value, Number maximum) {
  return value <= maximum ? value : maximum;
}

template <typename Number, typename Limit>
Number clamp(Number value, Limit minValue, Limit maxValue) {
  if (value <= minValue) return minValue;
  if (value >= maxValue) return maxValue;
  return value;
}

float rgb2y(float r, float g, float b) { return +0.299f * r + 0.587f * g + 0.114f * b; }
float rgb2cb(float r, float g, float b) { return -0.16874f * r - 0.33126f * g + 0.5f * b; }
float rgb2cr(float r, float g, float b) { return +0.5f * r - 0.41869f * g - 0.08131f * b; }

void DCT(float block[8 * 8], uint8_t stride)
{
  const auto SqrtHalfSqrt = 1.306562965f;
  const auto InvSqrt = 0.707106781f;
  const auto HalfSqrtSqrt = 0.382683432f;
  const auto InvSqrtSqrt = 0.541196100f;

  auto& block0 = block[0];
  auto& block1 = block[1 * stride];
  auto& block2 = block[2 * stride];
  auto& block3 = block[3 * stride];
  auto& block4 = block[4 * stride];
  auto& block5 = block[5 * stride];
  auto& block6 = block[6 * stride];
  auto& block7 = block[7 * stride];

  auto add07 = block0 + block7;
  auto sub07 = block0 - block7;
  auto add16 = block1 + block6;
  auto sub16 = block1 - block6;
  auto add25 = block2 + block5;
  auto sub25 = block2 - block5;
  auto add34 = block3 + block4;
  auto sub34 = block3 - block4;

  auto add0347 = add07 + add34;
  auto sub07_34 = add07 - add34;
  auto add1256 = add16 + add25;
  auto sub16_25 = add16 - add25;

  block0 = add0347 + add1256;
  block4 = add0347 - add1256;

  auto z1 = (sub16_25 + sub07_34) * InvSqrt;
  block2 = sub07_34 + z1;
  block6 = sub07_34 - z1;

  auto sub23_45 = sub25 + sub34;
  auto sub12_56 = sub16 + sub25;
  auto sub01_67 = sub16 + sub07;

  auto z5 = (sub23_45 - sub01_67) * HalfSqrtSqrt;
  auto z2 = sub23_45 * InvSqrtSqrt + z5;
  auto z3 = sub12_56 * InvSqrt;
  auto z4 = sub01_67 * SqrtHalfSqrt + z5;
  auto z6 = sub07 + z3;
  auto z7 = sub07 - z3;
  block1 = z6 + z4;
  block7 = z6 - z4;
  block5 = z7 + z2;
  block3 = z7 - z2;
}

int16_t encodeBlock(BitWriter& writer, float block[8][8], const float scaled[8 * 8], int16_t lastDC,
                    const BitCode huffmanDC[256], const BitCode huffmanAC[256], const BitCode* codewords) {
  auto block64 = (float*)block;

  for (auto offset = 0; offset < 8; offset++) DCT(block64 + offset * 8, 1);
  for (auto offset = 0; offset < 8; offset++) DCT(block64 + offset * 1, 8);

  for (auto i = 0; i < 8 * 8; i++) block64[i] *= scaled[i];

  auto DC = int(block64[0] + (block64[0] >= 0 ? +0.5f : -0.5f));

  auto posNonZero = 0;
  int16_t quantized[8 * 8];
  for (auto i = 1; i < 8 * 8; i++)
  {
    auto value = block64[ZigZagInv[i]];
    quantized[i] = int(value + (value >= 0 ? +0.5f : -0.5f));
    if (quantized[i] != 0) posNonZero = i;
  }

  auto diff = DC - lastDC;
  if (diff == 0)
    writer << huffmanDC[0x00];
  else {
    auto bits = codewords[diff];
    writer << huffmanDC[bits.numBits] << bits;
  }

  auto offset = 0;
  for (auto i = 1; i <= posNonZero; i++)
  {
    while (quantized[i] == 0)
    {
      offset += 0x10;
      if (offset > 0xF0)
      {
        writer << huffmanAC[0xF0];
        offset = 0;
      }
      i++;
    }

    auto encoded = codewords[quantized[i]];
    writer << huffmanAC[offset + encoded.numBits] << encoded;
    offset = 0;
  }

  if (posNonZero < 8 * 8 - 1)
    writer << huffmanAC[0x00];

  return DC;
}

void generateHuffmanTable(const uint8_t numCodes[16], const uint8_t* values, BitCode result[256]) {
  auto huffmanCode = 0;
  for (auto numBits = 1; numBits <= 16; numBits++) {
    for (auto i = 0; i < numCodes[numBits - 1]; i++)
      result[*values++] = BitCode(huffmanCode++, numBits);

    huffmanCode <<= 1;
  }
}

}

namespace TooJpeg {
bool writeJpeg(WRITE_ONE_BYTE output, const void* pixels_, unsigned short width, unsigned short height, bool isRGB,
               unsigned char quality_, bool downsample, const char* comment) {
  if (output == nullptr || pixels_ == nullptr) return false;
  if (width == 0 || height == 0) return false;

  const auto numComponents = isRGB ? 3 : 1;

  if (!isRGB) downsample = false;

  BitWriter bitWriter(output);

  const uint8_t HeaderJfif[2 + 2 + 16] = {
      0xFF, 0xD8,
      0xFF, 0xE0,
      0,    16,
      'J',  'F',  'I', 'F', 0,
      1,    1,
      0,
      0,    1,    0,   1,
      0,    0};
  bitWriter << HeaderJfif;

  if (comment != nullptr) {
    auto length = 0;
    while (comment[length] != 0) length++;

    bitWriter.addMarker(
        0xFE, 2 + length);
    for (auto i = 0; i < length; i++) bitWriter << comment[i];
  }

  auto quality = clamp<uint16_t>(quality_, 1, 100);
  quality = quality < 50 ? 5000 / quality : 200 - quality * 2;

  uint8_t quantLuminance[8 * 8];
  uint8_t quantChrominance[8 * 8];
  for (auto i = 0; i < 8 * 8; i++) {
    int luminance = (DefaultQuantLuminance[ZigZagInv[i]] * quality + 50) / 100;
    int chrominance = (DefaultQuantChrominance[ZigZagInv[i]] * quality + 50) / 100;

    quantLuminance[i] = clamp(luminance, 1, 255);
    quantChrominance[i] = clamp(chrominance, 1, 255);
  }

  bitWriter.addMarker(0xDB,
                      2 + (isRGB ? 2 : 1) * (1 + 8 * 8));

  bitWriter << 0x00 << quantLuminance;
  if (isRGB) bitWriter << 0x01 << quantChrominance;

  bitWriter.addMarker(
      0xC0, 2 + 6 + 3 * numComponents);

  bitWriter << 0x08
            << (height >> 8) << (height & 0xFF) << (width >> 8) << (width & 0xFF);

  bitWriter << numComponents;
  for (auto id = 1; id <= numComponents; id++)
    bitWriter << id
              << (id == 1 && downsample ? 0x22 : 0x11)
              << (id == 1 ? 0 : 1);

  bitWriter.addMarker(0xC4, isRGB ? (2 + 208 + 208) : (2 + 208));

  bitWriter << 0x00
            << DcLuminanceCodesPerBitsize << DcLuminanceValues;
  bitWriter << 0x10
            << AcLuminanceCodesPerBitsize << AcLuminanceValues;

  BitCode* huffmanLuminanceDC = (BitCode*)std::malloc(256 * sizeof(BitCode));
  BitCode* huffmanLuminanceAC = (BitCode*)std::malloc(256 * sizeof(BitCode));
  BitCode* huffmanChrominanceDC = nullptr;
  BitCode* huffmanChrominanceAC = nullptr;
  float* scaledLuminance = (float*)std::malloc(8 * 8 * sizeof(float));
  float* scaledChrominance = nullptr;
  BitCode* codewordsArray = (BitCode*)std::malloc(2 * CodeWordLimit * sizeof(BitCode));
  float (*Y)[8] = (float (*)[8])std::malloc(8 * 8 * sizeof(float));
  float (*Cb)[8] = nullptr;
  float (*Cr)[8] = nullptr;
  if (isRGB) {
    huffmanChrominanceDC = (BitCode*)std::malloc(256 * sizeof(BitCode));
    huffmanChrominanceAC = (BitCode*)std::malloc(256 * sizeof(BitCode));
    scaledChrominance = (float*)std::malloc(8 * 8 * sizeof(float));
    Cb = (float (*)[8])std::malloc(8 * 8 * sizeof(float));
    Cr = (float (*)[8])std::malloc(8 * 8 * sizeof(float));
  }
  auto cleanup = [&]() {
    std::free(huffmanLuminanceDC);
    std::free(huffmanLuminanceAC);
    std::free(huffmanChrominanceDC);
    std::free(huffmanChrominanceAC);
    std::free(scaledLuminance);
    std::free(scaledChrominance);
    std::free(codewordsArray);
    std::free(Y);
    std::free(Cb);
    std::free(Cr);
  };
  if (!huffmanLuminanceDC || !huffmanLuminanceAC || !scaledLuminance || !codewordsArray || !Y ||
      (isRGB && (!huffmanChrominanceDC || !huffmanChrominanceAC || !scaledChrominance || !Cb || !Cr))) {
    cleanup();
    return false;
  }
  generateHuffmanTable(DcLuminanceCodesPerBitsize, DcLuminanceValues, huffmanLuminanceDC);
  generateHuffmanTable(AcLuminanceCodesPerBitsize, AcLuminanceValues, huffmanLuminanceAC);

  if (isRGB) {
    bitWriter << 0x01
              << DcChrominanceCodesPerBitsize << DcChrominanceValues;
    bitWriter << 0x11
              << AcChrominanceCodesPerBitsize << AcChrominanceValues;

    generateHuffmanTable(DcChrominanceCodesPerBitsize, DcChrominanceValues, huffmanChrominanceDC);
    generateHuffmanTable(AcChrominanceCodesPerBitsize, AcChrominanceValues, huffmanChrominanceAC);
  }

  bitWriter.addMarker(
      0xDA, 2 + 1 + 2 * numComponents + 3);

  bitWriter << numComponents;
  for (auto id = 1; id <= numComponents; id++)
    bitWriter << id << (id == 1 ? 0x00 : 0x11);

  static const uint8_t Spectral[3] = {
      0, 63, 0};
  bitWriter << Spectral;

  for (auto i = 0; i < 8 * 8; i++) {
    auto row = ZigZagInv[i] / 8;
    auto column = ZigZagInv[i] % 8;

    static const float AanScaleFactors[8] = {1, 1.387039845f, 1.306562965f, 1.175875602f,
                                             1, 0.785694958f, 0.541196100f, 0.275899379f};
    auto factor = 1 / (AanScaleFactors[row] * AanScaleFactors[column] * 8);
    scaledLuminance[ZigZagInv[i]] = factor / quantLuminance[i];
    if (isRGB) scaledChrominance[ZigZagInv[i]] = factor / quantChrominance[i];
  }

  BitCode* codewords =
      &codewordsArray[CodeWordLimit];
  uint8_t numBits = 1;
  int mask = 1;
  for (int16_t value = 1; value < CodeWordLimit; value++) {
    if (value > mask)
    {
      numBits++;
      mask = (mask << 1) | 1;
    }
    codewords[-value] = BitCode(
        mask - value,
        numBits);
    codewords[+value] = BitCode(value, numBits);
  }

  auto pixels = (const uint8_t*)pixels_;

  const auto maxWidth = width - 1;
  const auto maxHeight = height - 1;

  const auto sampling = downsample ? 2 : 1;
  const auto mcuSize = 8 * sampling;

  int16_t lastYDC = 0, lastCbDC = 0, lastCrDC = 0;

  for (auto mcuY = 0; mcuY < height; mcuY += mcuSize)
    for (auto mcuX = 0; mcuX < width; mcuX += mcuSize) {
      for (auto blockY = 0; blockY < mcuSize; blockY += 8)
        for (auto blockX = 0; blockX < mcuSize; blockX += 8) {
          for (auto deltaY = 0; deltaY < 8; deltaY++) {
            auto column =
                minimum(mcuX + blockX, maxWidth);
            auto row = minimum(mcuY + blockY + deltaY, maxHeight);
            for (auto deltaX = 0; deltaX < 8; deltaX++) {
              auto pixelPos =
                  row * int(width) + column;
              if (column < maxWidth) column++;

              if (!isRGB) {
                Y[deltaY][deltaX] = pixels[pixelPos] - 128.f;
                continue;
              }

              auto r = pixels[3 * pixelPos];
              auto g = pixels[3 * pixelPos + 1];
              auto b = pixels[3 * pixelPos + 2];

              Y[deltaY][deltaX] = rgb2y(r, g, b) - 128;
              if (!downsample) {
                Cb[deltaY][deltaX] = rgb2cb(r, g, b);
                Cr[deltaY][deltaX] = rgb2cr(r, g, b);
              }
            }
          }

          lastYDC =
              encodeBlock(bitWriter, Y, scaledLuminance, lastYDC, huffmanLuminanceDC, huffmanLuminanceAC, codewords);
        }

      if (!isRGB) continue;

      if (downsample)
        for (short deltaY = 7; downsample && deltaY >= 0;
             deltaY--)
        {
          auto row = minimum(mcuY + 2 * deltaY, maxHeight);
          auto column = mcuX;
          auto pixelPos = (row * int(width) + column) * 3;

          auto rowStep = (row < maxHeight) ? 3 * int(width) : 0;
          auto columnStep = (column < maxWidth) ? 3 : 0;

          for (short deltaX = 0; deltaX < 8; deltaX++) {
            auto right = pixelPos + columnStep;
            auto down = pixelPos + rowStep;
            auto downRight = pixelPos + columnStep + rowStep;

            auto r = short(pixels[pixelPos]) + pixels[right] + pixels[down] + pixels[downRight];
            auto g = short(pixels[pixelPos + 1]) + pixels[right + 1] + pixels[down + 1] + pixels[downRight + 1];
            auto b = short(pixels[pixelPos + 2]) + pixels[right + 2] + pixels[down + 2] + pixels[downRight + 2];

            Cb[deltaY][deltaX] = rgb2cb(r, g, b) / 4;
            Cr[deltaY][deltaX] = rgb2cr(r, g, b) / 4;

            pixelPos += 2 * 3;
            column += 2;

            if (column >= maxWidth) {
              columnStep = 0;
              pixelPos = ((row + 1) * int(width) - 1) *
                         3;
            }
          }
        }

      lastCbDC = encodeBlock(bitWriter, Cb, scaledChrominance, lastCbDC, huffmanChrominanceDC, huffmanChrominanceAC,
                             codewords);
      lastCrDC = encodeBlock(bitWriter, Cr, scaledChrominance, lastCrDC, huffmanChrominanceDC, huffmanChrominanceAC,
                             codewords);
    }

  bitWriter.flush();

  bitWriter << 0xFF << 0xD9;
  cleanup();
  return true;
}
}
