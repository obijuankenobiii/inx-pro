#pragma once

#include <cstddef>

class Activity;
class GfxRenderer;

class LibraryIndexRefresh {
 public:
  static void start(GfxRenderer& renderer, Activity* screen);
  static void render(GfxRenderer& renderer);
  static void finish(GfxRenderer& renderer, Activity* screen);
  static bool isRunning();
};
