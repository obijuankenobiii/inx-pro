#include "BleMapping.h"

#if FREEINK_CAP_BLE_HID_HOST

#include <SDCardManager.h>
#include <Serialization.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace {
constexpr char kMappingFile[] = "/.system/ble_mapping.bin";
constexpr char kLegacyMappingFile[] = "/.system/murphy_ble_mapping.bin";
constexpr uint32_t kMagic = 0x314D424Du;  // "MBM1"
constexpr uint8_t kVersion = 1;
constexpr uint8_t kMaxRemotes = 4;
constexpr uint8_t kMaxBindings = 8;

struct Binding {
  uint32_t code = 0;
  ble_mapping::Action action = ble_mapping::Action::None;
};

struct RemoteTable {
  char address[18] = {};
  uint8_t count = 0;
  Binding bindings[kMaxBindings] = {};
};

RemoteTable tables[kMaxRemotes];
uint8_t tableCount = 0;
bool loaded = false;
bool learning = false;

bool validAction(const ble_mapping::Action action) {
  return action == ble_mapping::Action::NextPage || action == ble_mapping::Action::PreviousPage;
}

bool validTable(const RemoteTable& table) {
  return table.address[0] != '\0' && table.count <= kMaxBindings;
}

RemoteTable* tableFor(const char* address, const bool create) {
  if (!address || address[0] == '\0') return nullptr;
  for (uint8_t i = 0; i < tableCount; ++i) {
    if (std::strncmp(tables[i].address, address, sizeof(tables[i].address)) == 0) return &tables[i];
  }
  if (!create || tableCount >= kMaxRemotes) return nullptr;
  RemoteTable& table = tables[tableCount++];
  std::strncpy(table.address, address, sizeof(table.address) - 1);
  table.address[sizeof(table.address) - 1] = '\0';
  return &table;
}

void save() {
  SdMan.mkdir("/.system");
  FsFile file;
  if (!SdMan.openFileForWrite("CPS", kMappingFile, file)) return;
  serialization::writePod(file, kMagic);
  serialization::writePod(file, kVersion);
  serialization::writePod(file, tableCount);
  for (uint8_t i = 0; i < tableCount; ++i) {
    serialization::writePod(file, tables[i]);
  }
  file.close();
}

bool openMappingFile(FsFile& file) {
  if (SdMan.openFileForRead("CPS", kMappingFile, file)) return true;
  // One-time compatibility for mappings saved by the earlier Murphy-only UI.
  return SdMan.openFileForRead("CPS", kLegacyMappingFile, file);
}

}  // namespace

namespace ble_mapping {

void load() {
  if (loaded) return;
  loaded = true;
  tableCount = 0;

  FsFile file;
  if (!openMappingFile(file)) return;
  uint32_t magic = 0;
  uint8_t version = 0;
  uint8_t storedCount = 0;
  serialization::readPod(file, magic);
  serialization::readPod(file, version);
  serialization::readPod(file, storedCount);
  if (magic != kMagic || version != kVersion || storedCount > kMaxRemotes) {
    file.close();
    return;
  }

  for (uint8_t i = 0; i < storedCount; ++i) {
    RemoteTable table;
    serialization::readPod(file, table);
    table.address[sizeof(table.address) - 1] = '\0';
    if (!validTable(table)) continue;
    table.count = std::min(table.count, kMaxBindings);
    uint8_t validCount = 0;
    for (uint8_t j = 0; j < table.count; ++j) {
      if (table.bindings[j].code == 0 || !validAction(table.bindings[j].action)) continue;
      table.bindings[validCount++] = table.bindings[j];
    }
    table.count = validCount;
    tables[tableCount++] = table;
  }
  file.close();
}

bool learningActive() { return learning; }

void setLearningActive(const bool active) { learning = active; }

const char* actionName(const Action action) {
  switch (action) {
    case Action::NextPage: return "Next page";
    case Action::PreviousPage: return "Previous page";
    case Action::None: break;
  }
  return "Not set";
}

Action lookup(const char* address, const uint32_t code) {
  load();
  const RemoteTable* table = tableFor(address, false);
  if (!table || code == 0) return Action::None;
  for (uint8_t i = 0; i < table->count; ++i) {
    if (table->bindings[i].code == code) return table->bindings[i].action;
  }
  return Action::None;
}

bool learn(const char* address, const uint32_t code, const Action action) {
  load();
  if (!address || address[0] == '\0' || code == 0 || !validAction(action)) return false;
  RemoteTable* table = tableFor(address, true);
  if (!table) return false;

  uint8_t kept = 0;
  for (uint8_t i = 0; i < table->count; ++i) {
    if (table->bindings[i].action == action || table->bindings[i].code == code) continue;
    table->bindings[kept++] = table->bindings[i];
  }
  if (kept >= kMaxBindings) return false;
  table->bindings[kept++] = Binding{code, action};
  table->count = kept;
  save();
  return true;
}

bool find(const char* address, const Action action, uint32_t& code) {
  load();
  const RemoteTable* table = tableFor(address, false);
  if (!table) return false;
  for (uint8_t i = 0; i < table->count; ++i) {
    if (table->bindings[i].action == action) {
      code = table->bindings[i].code;
      return true;
    }
  }
  return false;
}

void formatCode(char* out, const size_t outSize, const uint32_t code) {
  if (!out || outSize == 0) return;
  std::snprintf(out, outSize, "%u:%u=%02X", static_cast<unsigned>((code >> 16) & 0xFF),
                static_cast<unsigned>((code >> 8) & 0xFF), static_cast<unsigned>(code & 0xFF));
}

}  // namespace ble_mapping

#endif  // FREEINK_CAP_BLE_HID_HOST
