/**
 * @file PdfValueParser.cpp
 * @brief Definitions for PdfValueParser.
 */

#include "PdfValueParser.h"

PdfObject parseNextPdfValue(PdfLexer& lexer) { return parsePdfValue(lexer, lexer.next()); }

PdfObject parsePdfValue(PdfLexer& lexer, const PdfToken& tok) {
  switch (tok.type) {
    case PdfTokenType::Number: {
      if (tok.isInteger && tok.number >= 0) {
        const size_t save = lexer.position();
        const PdfToken second = lexer.next();
        if (second.type == PdfTokenType::Number && second.isInteger && second.number >= 0) {
          const size_t save2 = lexer.position();
          const PdfToken third = lexer.next();
          if (third.type == PdfTokenType::Keyword && third.text == "R") {
            return PdfObject::makeRef(static_cast<uint32_t>(tok.number), static_cast<uint16_t>(second.number));
          }
          lexer.seek(save2);
        }
        lexer.seek(save);
      }
      return tok.isInteger ? PdfObject::makeInt(static_cast<int64_t>(tok.number)) : PdfObject::makeReal(tok.number);
    }
    case PdfTokenType::Name:
      return PdfObject::makeName(tok.text);
    case PdfTokenType::StringLiteral:
      return PdfObject::makeString(tok.text);
    case PdfTokenType::ArrayStart: {
      PdfObject arr;
      arr.type = PdfObject::Type::Array;
      while (true) {
        const PdfToken next = lexer.next();
        if (next.type == PdfTokenType::ArrayEnd || next.type == PdfTokenType::Eof) break;
        arr.arrValue.push_back(parsePdfValue(lexer, next));
      }
      return arr;
    }
    case PdfTokenType::DictStart: {
      PdfObject dict;
      dict.type = PdfObject::Type::Dictionary;
      while (true) {
        const PdfToken keyTok = lexer.next();
        if (keyTok.type == PdfTokenType::DictEnd || keyTok.type == PdfTokenType::Eof) break;
        if (keyTok.type != PdfTokenType::Name) {
          continue;
        }
        PdfObject value = parseNextPdfValue(lexer);
        dict.dictValue[keyTok.text] = std::move(value);
      }
      return dict;
    }
    case PdfTokenType::Keyword: {
      if (tok.text == "true") return PdfObject::makeBool(true);
      if (tok.text == "false") return PdfObject::makeBool(false);
      return PdfObject::makeNull();
    }
    default:
      return PdfObject::makeNull();
  }
}
