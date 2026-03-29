#include "ImagePrefetch.h"

#include "../../../src/util/SdIoMutex.h"

namespace EpubImagePrefetch {

void lockIo() {
  SdIoMutex::lock();
}

void unlockIo() {
  SdIoMutex::unlock();
}

}  // namespace EpubImagePrefetch
