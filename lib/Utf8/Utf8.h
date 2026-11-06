#pragma once

/**
 * @file Utf8.h
 * @brief Public interface and types for Utf8.
 */

#include <cstdint>
#include <string>
#define REPLACEMENT_GLYPH 0xFFFD

uint32_t utf8NextCodepoint(const unsigned char** string);

/**
 * Normalizes reader text before word measurement/rendering.
 * Expands Unicode presentation-form ligatures and removes invisible EPUB
 * formatting artifacts that must not affect bionic prefix byte offsets.
 */
void utf8NormalizeReaderText(std::string& text);

size_t utf8RemoveLastChar(std::string& str);

void utf8TruncateChars(std::string& str, size_t numChars);

void utf8TruncateBytes(std::string& str, size_t maxBytes);
