#pragma once

/**
 * Detect and persist the current timezone offset for the connected network.
 * Returns true when a valid offset was received and applied.
 */
bool autoDetectTimeZone();
