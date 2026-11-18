// ESP32-S3 targets have octal PSRAM. Allocate NimBLE host pools externally;
// the controller and task stacks stay in internal RAM.
#pragma once

#ifdef BOARD_HAS_PSRAM
#include <sdkconfig.h>

#undef CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_INTERNAL
#define CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL 1
#define FREEINK_BLE_STACK_IN_PSRAM 1
#endif
