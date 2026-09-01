/**
 * @file PdfValueParser.h
 * @brief Recursive-descent parser for a single PDF value (number/name/string/array/dict/reference) from a
 * PdfLexer. Shared by PdfDocument (object graph) and PdfContentInterpreter (operand stack literals).
 */

#pragma once

#include "PdfLexer.h"
#include "PdfObject.h"

PdfObject parsePdfValue(PdfLexer& lexer, const PdfToken& tok);

PdfObject parseNextPdfValue(PdfLexer& lexer);
