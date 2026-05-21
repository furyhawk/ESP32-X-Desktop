#pragma once

#include "i2c_bsp.h"
#include "display_bsp.h"

void DesktopUI_Init(I2cMasterBus *i2c_bus, DisplayPort *display);

/**
 * Show a full-screen provisioning QR overlay.
 * Call from within an LVGL lock.
 * @param service_name  SoftAP SSID (e.g. "PROV_AABBCC")
 * @param service_key   SoftAP password
 * @param qr_payload    JSON string to encode in the QR code
 */
void DesktopUI_ShowProvisioningQR(const char *service_name, const char *service_key, const char *qr_payload);
