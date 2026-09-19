#pragma once

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>

inline int metadataRatingStars(const std::string& value) {
  char* end = nullptr;
  const float rating = std::strtof(value.c_str(), &end);
  if (end == value.c_str() || !std::isfinite(rating) || rating <= 0.0f) return 0;

  // Calibre stores ratings from 0 to 10; other EPUB metadata commonly uses 0 to 5.
  const float stars = rating > 5.0f ? rating / 2.0f : rating;
  return std::max(0, std::min(5, static_cast<int>(std::lround(stars))));
}
