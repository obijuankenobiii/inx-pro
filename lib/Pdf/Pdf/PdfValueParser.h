/**
 * @file PdfValueParser.h
 * @brief Recursive-descent parser for a single PDF value (number/name/string/array/dict/reference) from a
 * PdfLexer. Shared by PdfDocument (object graph) and PdfContentInterpreter (operand stack literals).
 */

#pragma once

#include "PdfLexer.h"
#include "PdfObject.h"

// Parses one value starting at the lexer's current position. `tok` is the already-read first token of the
// value (so callers that peeked to decide "is this a value or an operator" don't have to re-lex it).
// Handles the "N G R" indirect-reference lookahead for bare integers.
PdfObject parsePdfValue(PdfLexer& lexer, const PdfToken& tok);

// Convenience: reads the next token itself, then parses the value.
PdfObject parseNextPdfValue(PdfLexer& lexer);
