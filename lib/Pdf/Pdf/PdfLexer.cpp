/**
 * @file PdfLexer.cpp
 * @brief Definitions for PdfLexer.
 */

#include "PdfLexer.h"

#include <cctype>
#include <cstdlib>

bool PdfLexer::isWhitespace(const uint8_t c) {
  return c == 0x00 || c == 0x09 || c == 0x0A || c == 0x0C || c == 0x0D || c == 0x20;
}

bool PdfLexer::isDelimiter(const uint8_t c) {
  return c == '(' || c == ')' || c == '<' || c == '>' || c == '[' || c == ']' || c == '{' || c == '}' || c == '/' ||
         c == '%';
}

uint8_t PdfLexer::peekByte(const size_t ahead) const {
  const size_t p = pos_ + ahead;
  return p < size_ ? data_[p] : 0;
}

void PdfLexer::skipWhitespaceAndComments() {
  while (pos_ < size_) {
    const uint8_t c = data_[pos_];
    if (isWhitespace(c)) {
      pos_++;
      continue;
    }
    if (c == '%') {
      while (pos_ < size_ && data_[pos_] != '\n' && data_[pos_] != '\r') {
        pos_++;
      }
      continue;
    }
    break;
  }
}

PdfToken PdfLexer::peek() {
  const size_t saved = pos_;
  PdfToken tok = next();
  pos_ = saved;
  return tok;
}

PdfToken PdfLexer::next() {
  while (true) {
    skipWhitespaceAndComments();

    PdfToken tok;
    if (pos_ >= size_) {
      tok.type = PdfTokenType::Eof;
      return tok;
    }

    const uint8_t c = data_[pos_];
    if (c == ')') {
      pos_++;
      continue;
    }
    return nextToken(c);
  }
}

PdfToken PdfLexer::nextToken(const uint8_t c) {
  PdfToken tok;

  if (c == '/') {
    pos_++;
    return readName();
  }
  if (c == '(') {
    return readLiteralString();
  }
  if (c == '<') {
    return readAngleBracketToken();
  }
  if (c == '>') {
    if (peekByte(1) == '>') {
      pos_ += 2;
      tok.type = PdfTokenType::DictEnd;
      return tok;
    }
    pos_++;
    tok.type = PdfTokenType::Keyword;
    tok.text = ">";
    return tok;
  }
  if (c == '[') {
    pos_++;
    tok.type = PdfTokenType::ArrayStart;
    return tok;
  }
  if (c == ']') {
    pos_++;
    tok.type = PdfTokenType::ArrayEnd;
    return tok;
  }
  if (c == '{' || c == '}') {
    pos_++;
    tok.type = PdfTokenType::Keyword;
    tok.text = std::string(1, static_cast<char>(c));
    return tok;
  }
  if (c == '+' || c == '-' || c == '.' || std::isdigit(c)) {
    return readNumberOrKeyword();
  }

  return readNumberOrKeyword();
}

PdfToken PdfLexer::readNumberOrKeyword() {
  const size_t start = pos_;
  bool sawDigit = false;
  bool isNumeric = true;
  bool sawDot = false;

  size_t p = pos_;
  if (p < size_ && (data_[p] == '+' || data_[p] == '-')) {
    p++;
  }
  while (p < size_ && (std::isdigit(data_[p]) || data_[p] == '.')) {
    if (data_[p] == '.') {
      if (sawDot) {
        isNumeric = false;
        break;
      }
      sawDot = true;
    } else {
      sawDigit = true;
    }
    p++;
  }

  if (isNumeric && sawDigit) {
    pos_ = p;
    PdfToken tok;
    tok.type = PdfTokenType::Number;
    tok.isInteger = !sawDot;
    tok.number = std::atof(std::string(reinterpret_cast<const char*>(data_ + start), pos_ - start).c_str());
    return tok;
  }

  pos_ = start;
  while (pos_ < size_ && !isWhitespace(data_[pos_]) && !isDelimiter(data_[pos_])) {
    pos_++;
  }
  if (pos_ == start) {
    pos_++;
  }

  PdfToken tok;
  tok.type = PdfTokenType::Keyword;
  tok.text.assign(reinterpret_cast<const char*>(data_ + start), pos_ - start);
  return tok;
}

PdfToken PdfLexer::readName() {
  const size_t start = pos_;
  std::string out;
  while (pos_ < size_ && !isWhitespace(data_[pos_]) && !isDelimiter(data_[pos_])) {
    if (data_[pos_] == '#' && pos_ + 2 < size_ && std::isxdigit(data_[pos_ + 1]) && std::isxdigit(data_[pos_ + 2])) {
      char hex[3] = {static_cast<char>(data_[pos_ + 1]), static_cast<char>(data_[pos_ + 2]), 0};
      out.push_back(static_cast<char>(std::strtol(hex, nullptr, 16)));
      pos_ += 3;
    } else {
      out.push_back(static_cast<char>(data_[pos_]));
      pos_++;
    }
  }
  (void)start;
  PdfToken tok;
  tok.type = PdfTokenType::Name;
  tok.text = std::move(out);
  return tok;
}

PdfToken PdfLexer::readLiteralString() {
  pos_++;
  std::string out;
  int depth = 1;
  while (pos_ < size_ && depth > 0) {
    const uint8_t c = data_[pos_];
    if (c == '\\') {
      pos_++;
      if (pos_ >= size_) break;
      const uint8_t esc = data_[pos_];
      switch (esc) {
        case 'n': out.push_back('\n'); pos_++; break;
        case 'r': out.push_back('\r'); pos_++; break;
        case 't': out.push_back('\t'); pos_++; break;
        case 'b': out.push_back('\b'); pos_++; break;
        case 'f': out.push_back('\f'); pos_++; break;
        case '(': out.push_back('('); pos_++; break;
        case ')': out.push_back(')'); pos_++; break;
        case '\\': out.push_back('\\'); pos_++; break;
        case '\r':
          pos_++;
          if (pos_ < size_ && data_[pos_] == '\n') pos_++;
          break;
        case '\n':
          pos_++;
          break;
        default:
          if (esc >= '0' && esc <= '7') {
            int value = 0;
            int digits = 0;
            while (digits < 3 && pos_ < size_ && data_[pos_] >= '0' && data_[pos_] <= '7') {
              value = value * 8 + (data_[pos_] - '0');
              pos_++;
              digits++;
            }
            out.push_back(static_cast<char>(value & 0xFF));
          } else {
            out.push_back(static_cast<char>(esc));
            pos_++;
          }
          break;
      }
      continue;
    }
    if (c == '(') {
      depth++;
      out.push_back('(');
      pos_++;
      continue;
    }
    if (c == ')') {
      depth--;
      pos_++;
      if (depth > 0) out.push_back(')');
      continue;
    }
    out.push_back(static_cast<char>(c));
    pos_++;
  }

  PdfToken tok;
  tok.type = PdfTokenType::StringLiteral;
  tok.text = std::move(out);
  return tok;
}

PdfToken PdfLexer::readAngleBracketToken() {
  if (peekByte(1) == '<') {
    pos_ += 2;
    PdfToken tok;
    tok.type = PdfTokenType::DictStart;
    return tok;
  }

  pos_++;
  std::string out;
  int hi = -1;
  while (pos_ < size_ && data_[pos_] != '>') {
    const uint8_t c = data_[pos_];
    pos_++;
    if (isWhitespace(c)) continue;
    int v;
    if (c >= '0' && c <= '9') v = c - '0';
    else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
    else continue;

    if (hi < 0) {
      hi = v;
    } else {
      out.push_back(static_cast<char>((hi << 4) | v));
      hi = -1;
    }
  }
  if (hi >= 0) {
    out.push_back(static_cast<char>(hi << 4));
  }
  if (pos_ < size_ && data_[pos_] == '>') pos_++;

  PdfToken tok;
  tok.type = PdfTokenType::StringLiteral;
  tok.text = std::move(out);
  return tok;
}
