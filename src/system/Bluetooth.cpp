#include "Bluetooth.h"

#if FREEINK_CAP_BLE_HID_HOST

#include <Arduino.h>
#include <BleKeyboardHost.h>
#include <SDCardManager.h>
#include <Serialization.h>

#include <atomic>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {
constexpr char kPreferencesFile[] = "/.system/bluetooth.bin";
constexpr uint8_t kPreferencesVersion = 1;

std::atomic<bool> startInFlight{false};
std::atomic<bool> stopRequested{false};
std::atomic<bool> preferenceLoaded{false};
std::atomic<bool> bluetoothEnabledState{false};

void loadPreference() {
  bool enabled = false;
  FsFile file;
  if (SdMan.openFileForRead("CPS", kPreferencesFile, file)) {
    uint8_t version = 0;
    uint8_t storedValue = 0;
    serialization::readPod(file, version);
    serialization::readPod(file, storedValue);
    file.close();
    if (version == kPreferencesVersion && storedValue <= 1) enabled = storedValue != 0;
  }
  bluetoothEnabledState.store(enabled, std::memory_order_release);
  preferenceLoaded.store(true, std::memory_order_release);
}

void ensurePreferenceLoaded() {
  if (!preferenceLoaded.load(std::memory_order_acquire)) loadPreference();
}

void startTaskEntry(void*) {
  if (!stopRequested.load(std::memory_order_acquire)) {
    const bool started = BleHid.begin("Inx Reader");
    INX_SERIAL.printf("[BLE] begin result=%d running=%d addr=%s\n", started ? 1 : 0,
                      BleHid.isRunning() ? 1 : 0, BleHid.connectedAddr());
  }
  startInFlight.store(false, std::memory_order_release);
  vTaskDelete(nullptr);
}
}  // namespace

bool bluetoothEnabled() {
  ensurePreferenceLoaded();
  return bluetoothEnabledState.load(std::memory_order_acquire);
}

bool setBluetoothEnabled(const bool enabled) {
  ensurePreferenceLoaded();
  bluetoothEnabledState.store(enabled, std::memory_order_release);

  FsFile file;
  SdMan.mkdir("/.system");
  if (!SdMan.openFileForWrite("CPS", kPreferencesFile, file)) return false;
  serialization::writePod(file, kPreferencesVersion);
  serialization::writePod(file, static_cast<uint8_t>(enabled ? 1 : 0));
  file.close();
  return true;
}

bool bluetoothRunning() { return BleHid.isRunning(); }

void startBluetooth() {
  if (!bluetoothEnabled()) return;
  stopRequested.store(false, std::memory_order_release);
  if (BleHid.isRunning() || BleHid.isStopping() || startInFlight.exchange(true)) return;
  if (xTaskCreate(startTaskEntry, "ble-start", 3072, nullptr, 2, nullptr) != pdPASS) {
    startInFlight.store(false, std::memory_order_release);
  }
}

void stopBluetooth() {
  stopRequested.store(true, std::memory_order_release);
  const uint32_t startedAt = millis();
  while (startInFlight.load(std::memory_order_acquire) && millis() - startedAt < 2000) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  if (BleHid.isRunning() || BleHid.isStopping()) BleHid.end(2000);
}

#else

bool bluetoothEnabled() { return false; }
bool setBluetoothEnabled(bool) { return false; }
bool bluetoothRunning() { return false; }
void startBluetooth() {}
void stopBluetooth() {}

#endif  // FREEINK_CAP_BLE_HID_HOST
