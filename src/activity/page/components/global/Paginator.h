#pragma once

class GfxRenderer;

class Paginator final {
 public:
  /** Renders the shared vertical outlined/filled page indicator used by paged pickers. */
  static void render(GfxRenderer& renderer, int activePage, int pageCount, int centerX, int top, int height);
};
