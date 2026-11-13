#pragma once

/**
 * @brief Return the ESP32-S3 USB pins to the Serial/JTAG flashing personality.
 *
 * USB Mass Storage and USB Serial/JTAG share the ESP32-S3 USB PHY. The
 * application must release the OTG personality before restarting, otherwise
 * the flashing port may remain unavailable until a physical reset.
 */
void handoffUsbOtgToSerialJtag();
