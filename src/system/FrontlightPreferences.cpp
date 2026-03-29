#include "system/FrontlightPreferences.h"

#include <SDCardManager.h>
#include <Serialization.h>

namespace {
constexpr char kPreferencesFile[] = "/.system/frontlight.bin";
constexpr uint8_t kVersion = 1;
}  // namespace

namespace frontlight_preferences {

bool load(Settings& settings) {
  FsFile file;
  if (!SdMan.openFileForRead("CPS", kPreferencesFile, file)) {
    return false;
  }

  uint8_t version = 0;
  serialization::readPod(file, version);
  if (version != kVersion) {
    file.close();
    return false;
  }

  serialization::readPod(file, settings.enabled);
  serialization::readPod(file, settings.brightness);
  serialization::readPod(file, settings.warmPercent);
  file.close();

  if (settings.enabled > 1) settings.enabled = 0;
  if (settings.brightness == 0 || settings.brightness > 100) settings.brightness = 50;
  if (settings.warmPercent > 100) settings.warmPercent = 50;
  return true;
}

bool save(const Settings& settings) {
  FsFile file;
  SdMan.mkdir("/.system");
  if (!SdMan.openFileForWrite("CPS", kPreferencesFile, file)) {
    return false;
  }

  serialization::writePod(file, kVersion);
  serialization::writePod(file, settings.enabled);
  serialization::writePod(file, settings.brightness);
  serialization::writePod(file, settings.warmPercent);
  file.close();
  return true;
}

}  // namespace frontlight_preferences
