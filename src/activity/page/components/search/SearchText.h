#pragma once

#include <string>

class GfxRenderer;

class SearchText {
 public:
  static constexpr int height = 56;

  static int top();
  static void render(const GfxRenderer& renderer, const std::string& value);
};
