#pragma once

#include <cstdint>
#include <string>

/**
 * Shared SD lock for the EPUB pipeline. Chapter layout, visible image extraction,
 * and the durable image cache all use this same ownership boundary. There is no
 * background EPUB image worker: it competed for the shared SD/display SPI bus
 * and made page-turn latency less predictable.
 */
namespace EpubImagePrefetch {

/** Serializes SdFat operations shared by the parser, renderer, and caches. */
void lockIo();
void unlockIo();

class IoLock final {
 public:
  IoLock() { lockIo(); }
  ~IoLock() { unlockIo(); }
  IoLock(const IoLock&) = delete;
  IoLock& operator=(const IoLock&) = delete;
};

}  // namespace EpubImagePrefetch
