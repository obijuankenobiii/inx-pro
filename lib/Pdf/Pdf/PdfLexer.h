/**
 * @file PdfLexer.h
 * @brief Tokenizer for PDF syntax (object graph + content streams), operating on an in-memory byte buffer.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

enum class PdfTokenType {
  Eof,
  Number,
  Name,
  StringLiteral,
  ArrayStart,
  ArrayEnd,
  DictStart,
  DictEnd,
  Keyword,
};

struct PdfToken {
  PdfTokenType type = PdfTokenType::Eof;
  double number = 0.0;
  bool isInteger = false;
  std::string text;
};

class PdfLexer {
 public:
  PdfLexer(const uint8_t* data, size_t size, size_t pos = 0) : data_(data), size_(size), pos_(pos) {}

  size_t position() const { return pos_; }
  void seek(size_t pos) { pos_ = pos < size_ ? pos : size_; }

  PdfToken next();
  PdfToken peek();

  static bool isWhitespace(uint8_t c);
  static bool isDelimiter(uint8_t c);

 private:
  const uint8_t* data_;
  size_t size_;
  size_t pos_;

  uint8_t peekByte(size_t ahead = 0) const;
  void skipWhitespaceAndComments();
  PdfToken nextToken(uint8_t firstByte);
  PdfToken readNumberOrKeyword();
  PdfToken readName();
  PdfToken readLiteralString();
  PdfToken readAngleBracketToken();
};
