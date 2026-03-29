#pragma once

#include <string>

class GfxRenderer;

namespace home {

bool preview(GfxRenderer& renderer, const std::string& cachePath, int spine, int page);

}  // namespace home
