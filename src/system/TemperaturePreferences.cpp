#include "system/TemperaturePreferences.h"

#include <SDCardManager.h>
#include <Serialization.h>

namespace {
constexpr char kPreferencesFile[] = "/.system/temperature.bin";
constexpr uint8_t kVersion = 1;
}  // namespace

namespace temperature_preferences {

bool loadFahrenheit(bool& fahrenheit) {
  FsFile file;
  if (!SdMan.openFileForRead("CPS", kPreferencesFile, file)) return false;

  uint8_t version = 0;
  uint8_t storedValue = 0;
  serialization::readPod(file, version);
  serialization::readPod(file, storedValue);
  file.close();
  if (version != kVersion || storedValue > 1) return false;

  fahrenheit = storedValue != 0;
  return true;
}

bool saveFahrenheit(const bool fahrenheit) {
  FsFile file;
  SdMan.mkdir("/.system");
  if (!SdMan.openFileForWrite("CPS", kPreferencesFile, file)) return false;

  serialization::writePod(file, kVersion);
  serialization::writePod(file, static_cast<uint8_t>(fahrenheit ? 1 : 0));
  file.close();
  return true;
}

}  // namespace temperature_preferences
