#pragma once

#include <cstdint>
#include <ctime>

// Success requires a new SNTP completion, not merely an already-valid clock.
// Stops SNTP on return and leaves utcNow unchanged on failure.
bool syncSystemClockWithNtp(time_t& utcNow, uint32_t timeoutMs);
