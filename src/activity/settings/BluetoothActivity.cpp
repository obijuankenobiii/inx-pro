#if FREEINK_CAP_BLE_HID_HOST

#include "BluetoothActivity.h"

#include <BleKeyboardHost.h>
#include <GfxRenderer.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>

#include "activity/page/SubPage.h"
#include "activity/page/components/global/Toggle.h"
#include "activity/settings/SettingsListLayout.h"
#include "images/LibraryFilterRight.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "system/BleMapping.h"
#include "system/Bluetooth.h"

namespace {
constexpr int kSideMargin = 20;
constexpr int kRightInset = 24;
constexpr int kCaretWidth = 32;
constexpr int kCaretHeight = 30;
constexpr int kRowHeight = SettingsListLayout::kRowHeight;
constexpr uint32_t kScanDurationMs = 10000;
constexpr uint32_t kLearnDurationMs = 15000;

constexpr ble_mapping::Action kMapActions[] = {ble_mapping::Action::NextPage, ble_mapping::Action::PreviousPage};

void drawRow(GfxRenderer& renderer, const int y, const char* label, const char* value, const bool hasCaret) {
  const int width = renderer.getScreenWidth();
  const int font = systemFontId();
  const int lineHeight = renderer.text.getLineHeight(font);
  const int textY = y + (kRowHeight - lineHeight) / 2;
  const int caretX = width - kRightInset - kCaretWidth;
  const int valueRight = hasCaret ? caretX - 12 : width - kRightInset;
  const int valueWidth = value && value[0] != '\0' ? renderer.text.getWidth(font, value) : 0;
  const int labelMaxWidth = valueWidth > 0 ? valueRight - kSideMargin - valueWidth - 18 : valueRight - kSideMargin;
  const std::string labelText = renderer.text.truncate(font, label, labelMaxWidth);

  renderer.text.render(font, kSideMargin, textY, labelText.c_str(), true, EpdFontFamily::REGULAR);
  if (valueWidth > 0) {
    renderer.text.render(font, valueRight - valueWidth, textY, value, true, EpdFontFamily::REGULAR);
  }
  if (hasCaret) {
    renderer.bitmap.icon(LibraryFilterRight, caretX, y + (kRowHeight - kCaretHeight) / 2,
                         kCaretWidth, kCaretHeight);
  }
  renderer.line.render(SettingsListLayout::kDividerInset, y + kRowHeight - 1,
                       width - SettingsListLayout::kDividerInset, y + kRowHeight - 1, true,
                       LineRender::Style::Dotted);
}

void drawToggleRow(GfxRenderer& renderer, const int y, const char* label, const bool enabled) {
  const int width = renderer.getScreenWidth();
  const int font = systemFontId();
  const int lineHeight = renderer.text.getLineHeight(font);
  const int textY = y + (kRowHeight - lineHeight) / 2;
  renderer.text.render(font, kSideMargin, textY, label, true, EpdFontFamily::REGULAR);
  Toggle::render(renderer, width - kRightInset, y, kRowHeight, enabled);
  renderer.line.render(SettingsListLayout::kDividerInset, y + kRowHeight - 1,
                       width - SettingsListLayout::kDividerInset, y + kRowHeight - 1, true,
                       LineRender::Style::Dotted);
}

const char* connectionState() {
  if (!bluetoothEnabled()) return "Off";
  if (!BleHid.isRunning()) return "Unavailable";
  if (BleHid.isConnected()) return "Connected";
  if (BleHid.isConnecting()) return "Connecting...";
  if (BleHid.isScanning()) return "Scanning...";
  return "Not connected";
}

const char* connectedFriendlyName(const char* addr) {
  if (!addr || !BleHid.isConnected() || std::strcmp(addr, BleHid.connectedAddr()) != 0) return nullptr;
  const char* name = BleHid.connectedName();
  if (!name || name[0] == '\0' || std::strcmp(name, addr) == 0) return nullptr;
  return name;
}

const char* pairedDisplayName(const freeink::PairedHidDevice& peer) {
  if (const char* name = connectedFriendlyName(peer.addr)) return name;
  if (peer.name[0] != '\0' && std::strcmp(peer.name, peer.addr) != 0) return peer.name;
  return "BLE device";
}

const char* discoveredDisplayName(const freeink::DiscoveredDevice& device) {
  if (const char* name = connectedFriendlyName(device.addr)) return name;
  if (device.hasName && device.name[0] != '\0') return device.name;
  return "BLE device";
}

}

void BluetoothActivity::onEnter() {
  Activity::onEnter();
  ble_mapping::setLearningActive(false);
  if (bluetoothEnabled()) startBluetooth();
  view_ = View::Menu;
  learningAction_ = -1;
  detailAddr_[0] = '\0';
  detailName_[0] = '\0';
  renderedSignature_ = 0;
  render();
}

void BluetoothActivity::onExit() {
  ble_mapping::setLearningActive(false);
  if (BleHid.isScanning()) BleHid.stopScan();
  Activity::onExit();
}

uint32_t BluetoothActivity::signature() const {
  const uint32_t state = (bluetoothEnabled() ? 8u : 0u) |
                         (BleHid.isConnected() ? 1u : BleHid.isConnecting() ? 2u : BleHid.isScanning() ? 4u : 0u);
  uint32_t view = 0;
  if (view_ == View::Devices) view = 1;
  if (view_ == View::Device) view = 2;
  if (view_ == View::Scan) view = 3;
  if (view_ == View::Mapping) view = 4;
  return state | static_cast<uint32_t>(BleHid.deviceCount()) << 8 |
         static_cast<uint32_t>(BleHid.pairedCount()) << 16 | view << 24 |
         static_cast<uint32_t>(learningAction_ + 1) << 28;
}

bool BluetoothActivity::detailIsConnected() const {
  return detailAddr_[0] != '\0' && BleHid.isConnected() &&
         std::strcmp(BleHid.connectedAddr(), detailAddr_) == 0;
}

const char* BluetoothActivity::detailTitle() const {
  if (const char* name = connectedFriendlyName(detailAddr_)) return name;
  if (detailName_[0] != '\0' && std::strcmp(detailName_, detailAddr_) != 0) return detailName_;
  return "BLE device";
}

void BluetoothActivity::render() {
  renderer.clearScreen();
  const char* title = "Bluetooth";
  if (view_ == View::Devices) title = "Devices";
  if (view_ == View::Device) title = detailTitle();
  if (view_ == View::Scan) title = "Scan for remote";
  if (view_ == View::Mapping) title = "Map buttons";
  const int bodyTop = SubPage::header(renderer, title);
  const int font = systemFontId();
  int y = bodyTop;

  if (view_ == View::Menu) {
    drawToggleRow(renderer, y, bluetoothEnabled() ? "Disable" : "Enable", bluetoothEnabled());
    y += kRowHeight;
    drawRow(renderer, y, "Scan", "", true);
    y += kRowHeight;
    char count[12] = {};
    if (BleHid.pairedCount() == 0) {
      std::snprintf(count, sizeof(count), "None");
    } else {
      std::snprintf(count, sizeof(count), "%u", static_cast<unsigned>(BleHid.pairedCount()));
    }
    drawRow(renderer, y, "Devices", count, true);
  } else if (view_ == View::Devices) {
    const uint8_t paired = BleHid.pairedCount();
    if (paired == 0) {
      drawRow(renderer, y, "No paired remotes", "", false);
    } else {
      for (uint8_t i = 0; i < paired; ++i) {
        const auto& peer = BleHid.paired(i);
        const char* label = pairedDisplayName(peer);
        const char* value = std::strcmp(peer.addr, BleHid.connectedAddr()) == 0 ? "Connected" : "";
        drawRow(renderer, y, label, value, true);
        y += kRowHeight;
      }
    }
  } else if (view_ == View::Device) {
    drawRow(renderer, y, "Status", connectionState(), true);
    y += kRowHeight;
    drawRow(renderer, y, "Map buttons", "", true);
  } else if (view_ == View::Mapping) {
    for (size_t i = 0; i < std::size(kMapActions); ++i) {
      char value[24] = {};
      uint32_t code = 0;
      if (ble_mapping::find(BleHid.connectedAddr(), kMapActions[i], code)) {
        ble_mapping::formatCode(value, sizeof(value), code);
      } else {
        std::snprintf(value, sizeof(value), "Not set");
      }
      if (learningAction_ == static_cast<int>(i)) {
        std::snprintf(value, sizeof(value), "Press remote button");
      }
      drawRow(renderer, y, ble_mapping::actionName(kMapActions[i]), value, false);
      y += kRowHeight;
    }
  } else {
    drawRow(renderer, y, BleHid.isScanning() ? "Stop scan" : "Scan again", "", true);
    y += kRowHeight;
    const uint8_t found = BleHid.deviceCount();
    if (found == 0) {
      drawRow(renderer, y, BleHid.isScanning() ? "Looking for HID remotes..." : "No remotes found", "", false);
    } else {
      for (uint8_t i = 0; i < found; ++i) {
        const auto& device = BleHid.device(i);
        char value[16] = {};
        std::snprintf(value, sizeof(value), "%d dBm", device.rssi);
        drawRow(renderer, y, discoveredDisplayName(device), value, true);
        y += kRowHeight;
      }
    }
  }

  mappedInput.mapLabels("\xC2\xAB Back", "", "", "");
  renderer.displayBuffer();
  renderedSignature_ = signature();
}

void BluetoothActivity::startScan() {
  if (!bluetoothEnabled()) return;
  if (!BleHid.isRunning()) {
    startBluetooth();
    return;
  }
  BleHid.startScan(kScanDurationMs);
  view_ = View::Scan;
  render();
}

void BluetoothActivity::openDevices() {
  view_ = View::Devices;
  render();
}

void BluetoothActivity::openDevice(const char* addr, const char* name) {
  std::snprintf(detailAddr_, sizeof(detailAddr_), "%s", addr ? addr : "");
  std::snprintf(detailName_, sizeof(detailName_), "%s", name ? name : "");
  if (detailAddr_[0] != '\0' && std::strcmp(detailAddr_, BleHid.connectedAddr()) != 0) {
    BleHid.connect(detailAddr_);
  }
  view_ = View::Device;
  render();
}

void BluetoothActivity::openPairedDevice(const int index) {
  if (index < 0 || index >= BleHid.pairedCount()) return;
  const auto& peer = BleHid.paired(static_cast<uint8_t>(index));
  openDevice(peer.addr, peer.name[0] ? peer.name : "");
}

void BluetoothActivity::openDiscoveredDevice(const int index) {
  if (index < 0 || index >= BleHid.deviceCount()) return;
  const auto& device = BleHid.device(static_cast<uint8_t>(index));
  openDevice(device.addr, device.hasName ? device.name : "");
  BleHid.releaseScanResults();
}

void BluetoothActivity::startLearning(const int actionIndex) {
  if (actionIndex < 0 || actionIndex >= static_cast<int>(std::size(kMapActions)) || !detailIsConnected()) return;
  freeink::RawButtonEvent event;
  while (BleHid.popRawButton(event)) {
  }
  learningAction_ = actionIndex;
  learningStartedAt_ = millis();
  ble_mapping::setLearningActive(true);
  render();
}

void BluetoothActivity::finishLearning() {
  learningAction_ = -1;
  ble_mapping::setLearningActive(false);
  render();
}

void BluetoothActivity::handleMappingInput() {
  if (learningAction_ < 0) return;

  freeink::RawButtonEvent event;
  while (BleHid.popRawButton(event)) {
    INX_SERIAL.printf("[BLE_MAP] raw pressed=%d rest=%d code=%06lX report=%u byte=%u value=%02X key=%02X mods=%02X\n",
                      event.pressed ? 1 : 0, event.wasRest ? 1 : 0, static_cast<unsigned long>(event.code()),
                      event.reportId, event.byteIndex, event.value, event.keycode, event.mods);
    if (!event.pressed || event.wasRest || event.code() == 0) continue;
    const bool saved = ble_mapping::learn(BleHid.connectedAddr(), event.code(), kMapActions[learningAction_]);
    INX_SERIAL.printf("[BLE_MAP] learn action=%s saved=%d addr=%s\n",
                      ble_mapping::actionName(kMapActions[learningAction_]), saved ? 1 : 0, BleHid.connectedAddr());
    if (saved) finishLearning();
    return;
  }

  freeink::KeyEvent key;
  while (BleHid.popKey(key)) {
    INX_SERIAL.printf("[BLE_MAP] key pressed=%d key=%02X mods=%02X special=%u ch=%02X\n", key.pressed ? 1 : 0,
                      key.keycode, key.mods, static_cast<unsigned>(key.special), static_cast<unsigned char>(key.ch));
    if (!key.pressed || key.mods != 0 || key.keycode == 0) continue;
    const uint32_t code = 0xFFFF00u | key.keycode;
    const bool saved = ble_mapping::learn(BleHid.connectedAddr(), code, kMapActions[learningAction_]);
    INX_SERIAL.printf("[BLE_MAP] learn decoded action=%s saved=%d code=%06lX addr=%s\n",
                      ble_mapping::actionName(kMapActions[learningAction_]), saved ? 1 : 0,
                      static_cast<unsigned long>(code), BleHid.connectedAddr());
    if (saved) finishLearning();
    return;
  }

  if (millis() - learningStartedAt_ >= kLearnDurationMs) finishLearning();
}

bool BluetoothActivity::handleTouchInput() {
  const auto close = [this] {
    if (view_ == View::Mapping) {
      view_ = View::Device;
      learningAction_ = -1;
      ble_mapping::setLearningActive(false);
      render();
    } else if (view_ == View::Devices || view_ == View::Device || view_ == View::Scan) {
      if (view_ == View::Scan && BleHid.isScanning()) BleHid.stopScan();
      view_ = view_ == View::Device ? View::Devices : View::Menu;
      render();
    } else {
      onBack_();
    }
  };
  if (SubPage::closeInput(renderer, mappedInput, close)) return true;
  if (!mappedInput.hasTouch()) return false;
  if (view_ == View::Mapping && learningAction_ >= 0) {
    handleMappingInput();
    return true;
  }

  float tapNx = 0.0f;
  float tapNy = 0.0f;
  if (!mappedInput.wasTouchTapInScreen(renderer, tapNx, tapNy)) return false;

  const int tapY = static_cast<int>(tapNy * renderer.getScreenHeight());
  const int bodyTop = navigation::Menu::topPadding + navigation::Menu::iconSize + 20;
  const int row = (tapY - bodyTop) / kRowHeight;
  if (tapY < bodyTop || row < 0) return true;

  if (view_ == View::Menu) {
    if (row == 0) {
      const bool enabled = !bluetoothEnabled();
      setBluetoothEnabled(enabled);
      if (enabled) startBluetooth();
      else stopBluetooth();
      render();
    } else if (row == 1) {
      startScan();
    } else if (row == 2) {
      openDevices();
    }
    return true;
  }

  if (view_ == View::Devices) {
    if (row >= 0 && row < BleHid.pairedCount()) openPairedDevice(row);
    return true;
  }

  if (view_ == View::Device) {
    if (row == 0) {
      if (BleHid.isConnected() || BleHid.isConnecting()) {
        BleHid.disconnect();
      } else if (detailAddr_[0] != '\0') {
        BleHid.connect(detailAddr_);
      }
      render();
    } else if (row == 1 && detailIsConnected()) {
      view_ = View::Mapping;
      learningAction_ = -1;
      ble_mapping::setLearningActive(false);
      render();
    }
    return true;
  }

  if (view_ == View::Mapping) {
    const int mappingFirstRowY = bodyTop;
    const int mappingRow = (tapY - mappingFirstRowY) / kRowHeight;
    if (tapY >= mappingFirstRowY && mappingRow >= 0 && mappingRow < static_cast<int>(std::size(kMapActions))) {
      startLearning(mappingRow);
    }
    return true;
  }

  if (row == 0) {
    if (BleHid.isScanning()) BleHid.stopScan();
    else startScan();
    render();
  } else {
    openDiscoveredDevice(row - 1);
  }
  return true;
}

void BluetoothActivity::loop() {
  if (handleTouchInput()) return;
  handleMappingInput();

  const uint32_t currentSignature = signature();
  if (currentSignature != renderedSignature_) render();
}

#endif
