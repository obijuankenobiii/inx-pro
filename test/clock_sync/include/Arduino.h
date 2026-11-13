#pragma once

#include <cstdint>

unsigned long millis();
void vTaskDelay(uint32_t ms);
#define pdMS_TO_TICKS(ms) (ms)
