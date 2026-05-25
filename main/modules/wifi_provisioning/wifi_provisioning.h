#pragma once

#include <esp_err.h>
#include <stdbool.h>
#include <stddef.h>

esp_err_t WifiProvisioning_Bootstrap(void);

/** Clears saved Wi-Fi credentials and starts SoftAP provisioning again. */
esp_err_t WifiProvisioning_Reprovision(void);

/** Returns true if SoftAP provisioning is currently active (device not yet provisioned). */
bool WifiProvisioning_IsProvisioning(void);

/** Returns true when STA is connected and provisioning is not active. */
bool WifiProvisioning_IsConnected(void);

/** Returns true if the system clock appears to be valid for TLS certificate checks. */
bool WifiProvisioning_IsSystemTimeSynchronized(void);

/** Writes a short Wi-Fi connection status string for UI display. */
void WifiProvisioning_GetConnectionStatus(char *buf, size_t len);

/** Fills buf with the service name (e.g. "PROV_AABBCC"). */
void WifiProvisioning_GetServiceName(char *buf, size_t len);

/**
 * Fills buf with the JSON payload to encode in the QR code:
 * {"ver":"v1","name":"PROV_XX","username":"wifiprov","pop":"abcd1234","transport":"softap"}
 */
void WifiProvisioning_GetQRPayload(char *buf, size_t len);