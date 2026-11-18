#pragma once

// BLE lifecycle helpers. NimBLE startup runs on its own task because the
// controller initialization can exceed the framework IPC task stack.
bool bluetoothEnabled();
bool setBluetoothEnabled(bool enabled);
bool bluetoothRunning();
void startBluetooth();
void stopBluetooth();
