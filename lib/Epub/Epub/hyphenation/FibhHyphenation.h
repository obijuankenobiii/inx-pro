#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

bool isValidFibhHyphenation(const uint8_t* data, size_t size);
std::vector<size_t> fibhBreakIndexes(const std::string& word, const uint8_t* data, size_t size,
                                     size_t minPrefix = 2, size_t minSuffix = 3);
