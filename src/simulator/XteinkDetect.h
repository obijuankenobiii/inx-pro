#pragma once

// The native simulator already owns the display controller; the hardware
// Xteink probe is intentionally a no-op here.
namespace freeink {
inline void applyXteinkDisplayController() {}
}
