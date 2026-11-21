#pragma once

#include <cstdint>
#include <ctime>



bool syncSystemClockWithNtp(time_t& utcNow, uint32_t timeoutMs);
