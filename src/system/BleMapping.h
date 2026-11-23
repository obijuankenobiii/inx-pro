#pragma once

#include <cstddef>
#include <cstdint>

namespace ble_mapping {

enum class Action : uint8_t {
  None = 0,
  NextPage = 1,
  PreviousPage = 2,
};

void load();
bool learningActive();
void setLearningActive(bool active);
const char* actionName(Action action);
Action lookup(const char* address, uint32_t code);
bool learn(const char* address, uint32_t code, Action action);
bool find(const char* address, Action action, uint32_t& code);
void formatCode(char* out, size_t outSize, uint32_t code);

}
