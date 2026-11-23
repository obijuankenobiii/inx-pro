#include "ImagePrefetch.h"

#include "../../../src/util/SdIoMutex.h"

namespace EpubImagePrefetch {

void lockIo() {
  SdIoMutex::lock();
}

bool tryLockIo() {
  return SdIoMutex::tryLock();
}

void unlockIo() {
  SdIoMutex::unlock();
}

}
