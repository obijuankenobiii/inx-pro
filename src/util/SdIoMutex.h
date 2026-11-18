#pragma once

namespace SdIoMutex {

/** Serializes filesystem/SPI transactions across activity and cache tasks. */
void lock();
/** Attempts to acquire the filesystem/SPI lock without waiting. */
bool tryLock();
void unlock();

class Lock final {
 public:
  Lock() { lock(); }
  ~Lock() { unlock(); }
  Lock(const Lock&) = delete;
  Lock& operator=(const Lock&) = delete;
};

}
