#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

#include <esp_event.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_netif_sntp.h>
#include <esp_wifi.h>
#include <nvs_flash.h>

#include <network_provisioning/manager.h>
#include <network_provisioning/scheme_softap.h>

#include "desktop_ui.h"
#include "wifi_provisioning.h"

#define TAG "wifi_prov"
#define PROV_QR_VERSION "v1"
#define PROV_TRANSPORT_SOFTAP "softap"
#define QRCODE_BASE_URL "https://espressif.github.io/esp-jumpstart/qrcode.html"

static EventGroupHandle_t wifi_event_group;
static const int WIFI_CONNECTED_EVENT = BIT0;
static bool wifi_bootstrap_done = false;
static bool wifi_mgr_initialized = false;
static bool wifi_is_provisioning = false;
static bool wifi_sntp_initialized = false;
static bool wifi_is_connected = false;
static bool wifi_ps_overridden_for_prov = false;
static char prov_service_name[20] = {0};
static char prov_qr_payload[200] = {0};
static const char *SINGAPORE_TZ = "SGT-8";

/* Security-2 development credentials copied from Espressif provisioning example. */
static const char sec2_salt[] = {
    0x03, 0x6e, 0xe0, 0xc7, 0xbc, 0xb9, 0xed, 0xa8,
    0x4c, 0x9e, 0xac, 0x97, 0xd9, 0x3d, 0xec, 0xf4
};

static const char sec2_verifier[] = {
    0x7c, 0x7c, 0x85, 0x47, 0x65, 0x08, 0x94, 0x6d, 0xd6, 0x36, 0xaf, 0x37, 0xd7, 0xe8, 0x91, 0x43,
    0x78, 0xcf, 0xfd, 0x61, 0x6c, 0x59, 0xd2, 0xf8, 0x39, 0x08, 0x12, 0x72, 0x38, 0xde, 0x9e, 0x24,
    0xa4, 0x70, 0x26, 0x1c, 0xdf, 0xa9, 0x03, 0xc2, 0xb2, 0x70, 0xe7, 0xb1, 0x32, 0x24, 0xda, 0x11,
    0x1d, 0x97, 0x18, 0xdc, 0x60, 0x72, 0x08, 0xcc, 0x9a, 0xc9, 0x0c, 0x48, 0x27, 0xe2, 0xae, 0x89,
    0xaa, 0x16, 0x25, 0xb8, 0x04, 0xd2, 0x1a, 0x9b, 0x3a, 0x8f, 0x37, 0xf6, 0xe4, 0x3a, 0x71, 0x2e,
    0xe1, 0x27, 0x86, 0x6e, 0xad, 0xce, 0x28, 0xff, 0x54, 0x46, 0x60, 0x1f, 0xb9, 0x96, 0x87, 0xdc,
    0x57, 0x40, 0xa7, 0xd4, 0x6c, 0xc9, 0x77, 0x54, 0xdc, 0x16, 0x82, 0xf0, 0xed, 0x35, 0x6a, 0xc4,
    0x70, 0xad, 0x3d, 0x90, 0xb5, 0x81, 0x94, 0x70, 0xd7, 0xbc, 0x65, 0xb2, 0xd5, 0x18, 0xe0, 0x2e,
    0xc3, 0xa5, 0xf9, 0x68, 0xdd, 0x64, 0x7b, 0xb8, 0xb7, 0x3c, 0x9c, 0xfc, 0x00, 0xd8, 0x71, 0x7e,
    0xb7, 0x9a, 0x7c, 0xb1, 0xb7, 0xc2, 0xc3, 0x18, 0x34, 0x29, 0x32, 0x43, 0x3e, 0x00, 0x99, 0xe9,
    0x82, 0x94, 0xe3, 0xd8, 0x2a, 0xb0, 0x96, 0x29, 0xb7, 0xdf, 0x0e, 0x5f, 0x08, 0x33, 0x40, 0x76,
    0x52, 0x91, 0x32, 0x00, 0x9f, 0x97, 0x2c, 0x89, 0x6c, 0x39, 0x1e, 0xc8, 0x28, 0x05, 0x44, 0x17,
    0x3f, 0x68, 0x02, 0x8a, 0x9f, 0x44, 0x61, 0xd1, 0xf5, 0xa1, 0x7e, 0x5a, 0x70, 0xd2, 0xc7, 0x23,
    0x81, 0xcb, 0x38, 0x68, 0xe4, 0x2c, 0x20, 0xbc, 0x40, 0x57, 0x76, 0x17, 0xbd, 0x08, 0xb8, 0x96,
    0xbc, 0x26, 0xeb, 0x32, 0x46, 0x69, 0x35, 0x05, 0x8c, 0x15, 0x70, 0xd9, 0x1b, 0xe9, 0xbe, 0xcc,
    0xa9, 0x38, 0xa6, 0x67, 0xf0, 0xad, 0x50, 0x13, 0x19, 0x72, 0x64, 0xbf, 0x52, 0xc2, 0x34, 0xe2,
    0x1b, 0x11, 0x79, 0x74, 0x72, 0xbd, 0x34, 0x5b, 0xb1, 0xe2, 0xfd, 0x66, 0x73, 0xfe, 0x71, 0x64,
    0x74, 0xd0, 0x4e, 0xbc, 0x51, 0x24, 0x19, 0x40, 0x87, 0x0e, 0x92, 0x40, 0xe6, 0x21, 0xe7, 0x2d,
    0x4e, 0x37, 0x76, 0x2f, 0x2e, 0xe2, 0x68, 0xc7, 0x89, 0xe8, 0x32, 0x13, 0x42, 0x06, 0x84, 0x84,
    0x53, 0x4a, 0xb3, 0x0c, 0x1b, 0x4c, 0x8d, 0x1c, 0x51, 0x97, 0x19, 0xab, 0xae, 0x77, 0xff, 0xdb,
    0xec, 0xf0, 0x10, 0x95, 0x34, 0x33, 0x6b, 0xcb, 0x3e, 0x84, 0x0f, 0xb9, 0xd8, 0x5f, 0xb8, 0xa0,
    0xb8, 0x55, 0x53, 0x3e, 0x70, 0xf7, 0x18, 0xf5, 0xce, 0x7b, 0x4e, 0xbf, 0x27, 0xce, 0xce, 0xa8,
    0xb3, 0xbe, 0x40, 0xc5, 0xc5, 0x32, 0x29, 0x3e, 0x71, 0x64, 0x9e, 0xde, 0x8c, 0xf6, 0x75, 0xa1,
    0xe6, 0xf6, 0x53, 0xc8, 0x31, 0xa8, 0x78, 0xde, 0x50, 0x40, 0xf7, 0x62, 0xde, 0x36, 0xb2, 0xba
};

static void wifi_set_singapore_timezone(void)
{
    setenv("TZ", SINGAPORE_TZ, 1);
    tzset();
}

static void wifi_time_sync_cb(struct timeval *tv)
{
    (void)tv;

    wifi_set_singapore_timezone();

    esp_err_t ret = DesktopUI_SyncRtcFromSystemTime();
    if(ret == ESP_OK) {
        ESP_LOGI(TAG, "SNTP sync completed and RTC updated for Singapore local time");
    } else if(ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "SNTP sync completed, but RTC update failed (error: %d)", ret);
    }
}

static void wifi_start_time_sync(void)
{
    wifi_set_singapore_timezone();

    if(!wifi_sntp_initialized) {
        esp_sntp_config_t sntp_cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("time.google.com");
        sntp_cfg.wait_for_sync = false;
        sntp_cfg.sync_cb = wifi_time_sync_cb;

        esp_err_t ret = esp_netif_sntp_init(&sntp_cfg);
        if(ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to initialize SNTP (error: %d)", ret);
            return;
        }

        wifi_sntp_initialized = true;
        ESP_LOGI(TAG, "SNTP initialized using Singapore local time");
        return;
    }

    esp_err_t ret = esp_netif_sntp_start();
    if(ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to restart SNTP after reconnect (error: %d)", ret);
        return;
    }

    ESP_LOGI(TAG, "SNTP restart requested after Wi-Fi reconnect");
}

static void wifi_init_sta(void)
{
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
}

static void get_device_service_name(char *service_name, size_t max_len)
{
    uint8_t mac[6] = {0};
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    snprintf(service_name, max_len, "PROV_%02X%02X%02X", mac[3], mac[4], mac[5]);
}

static void wifi_prov_print_url(const char *name, const char *username, const char *pop)
{
    if(username != NULL && username[0] != '\0') {
        snprintf(prov_qr_payload, sizeof(prov_qr_payload),
                 "{\"ver\":\"%s\",\"name\":\"%s\",\"username\":\"%s\",\"pop\":\"%s\",\"transport\":\"%s\",\"network\":\"wifi\"}",
                 PROV_QR_VERSION, name, username, pop, PROV_TRANSPORT_SOFTAP);
    } else {
        snprintf(prov_qr_payload, sizeof(prov_qr_payload),
                 "{\"ver\":\"%s\",\"name\":\"%s\",\"pop\":\"%s\",\"transport\":\"%s\",\"network\":\"wifi\"}",
                 PROV_QR_VERSION, name, pop, PROV_TRANSPORT_SOFTAP);
    }

    ESP_LOGI(TAG, "Provisioning URL:\n%s?data=%s", QRCODE_BASE_URL, prov_qr_payload);
}

static esp_err_t wifi_prov_manager_init_if_needed(void)
{
    if(wifi_mgr_initialized) {
        return ESP_OK;
    }

    network_prov_mgr_config_t prov_cfg = {};
    prov_cfg.scheme = network_prov_scheme_softap;
    prov_cfg.scheme_event_handler = NETWORK_PROV_EVENT_HANDLER_NONE;

    esp_err_t ret = network_prov_mgr_init(prov_cfg);
    if(ret != ESP_OK) {
        return ret;
    }

    wifi_mgr_initialized = true;
    return ESP_OK;
}

static esp_err_t wifi_start_softap_provisioning(void)
{
    char service_name[16] = {0};
    const char *username = NULL;
    const char *pop = "abcd1234";
    const char *service_key = NULL;

    get_device_service_name(service_name, sizeof(service_name));
    strncpy(prov_service_name, service_name, sizeof(prov_service_name) - 1);
    prov_service_name[sizeof(prov_service_name) - 1] = '\0';

    /* Block normal STA reconnect flow while SoftAP provisioning is active. */
    wifi_is_provisioning = true;
    wifi_is_connected = false;
    xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_EVENT);

    if(!wifi_ps_overridden_for_prov) {
        esp_err_t ps_ret = esp_wifi_set_ps(WIFI_PS_NONE);
        if(ps_ret == ESP_OK) {
            wifi_ps_overridden_for_prov = true;
            ESP_LOGI(TAG, "Disabled Wi-Fi power save during provisioning");
        } else {
            ESP_LOGW(TAG, "Failed to disable Wi-Fi power save during provisioning (error: %d)", ps_ret);
        }
    }

    ESP_LOGI(TAG, "Starting SoftAP provisioning (service name: %s, auth: open + sec1)", service_name);
    esp_err_t ret = network_prov_mgr_start_provisioning(NETWORK_PROV_SECURITY_1,
                                                        (const void *)pop,
                                                        service_name,
                                                        service_key);
    if(ret != ESP_OK) {
        wifi_is_provisioning = false;
        return ret;
    }

    wifi_prov_print_url(service_name, username, pop);
    return ESP_OK;
}

static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if(event_base == NETWORK_PROV_EVENT) {
        switch(event_id) {
        case NETWORK_PROV_START:
            ESP_LOGI(TAG, "Provisioning started");
            break;
        case NETWORK_PROV_WIFI_CRED_RECV: {
            wifi_sta_config_t *sta_cfg = (wifi_sta_config_t *)event_data;
            ESP_LOGI(TAG, "Received Wi-Fi credentials for SSID: %s", (const char *)sta_cfg->ssid);
            break;
        }
        case NETWORK_PROV_WIFI_CRED_FAIL:
            ESP_LOGE(TAG, "Provisioning failed. Please retry from your phone.");
            break;
        case NETWORK_PROV_WIFI_CRED_SUCCESS:
            ESP_LOGI(TAG, "Provisioning successful");
            break;
        case NETWORK_PROV_END:
            ESP_LOGI(TAG, "Provisioning ended, deinitializing manager");
            ESP_ERROR_CHECK(network_prov_mgr_deinit());
            wifi_mgr_initialized = false;
            wifi_is_provisioning = false;
            if(wifi_ps_overridden_for_prov) {
                esp_err_t ps_ret = esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
                if(ps_ret != ESP_OK) {
                    ESP_LOGW(TAG, "Failed to restore Wi-Fi power save after provisioning (error: %d)", ps_ret);
                }
                wifi_ps_overridden_for_prov = false;
            }
            break;
        default:
            break;
        }
    } else if(event_base == WIFI_EVENT) {
        switch(event_id) {
        case WIFI_EVENT_STA_START:
            if(!wifi_is_provisioning) {
                esp_wifi_connect();
            }
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            wifi_is_connected = false;
            xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_EVENT);
            if(wifi_is_provisioning) {
                ESP_LOGI(TAG, "Wi-Fi STA disconnected during provisioning; reconnect suppressed");
            } else {
                ESP_LOGI(TAG, "Wi-Fi disconnected, retrying...");
                esp_wifi_connect();
            }
            break;
        case WIFI_EVENT_AP_STACONNECTED:
            ESP_LOGI(TAG, "Provisioning SoftAP client connected");
            break;
        case WIFI_EVENT_AP_STADISCONNECTED:
            ESP_LOGI(TAG, "Provisioning SoftAP client disconnected");
            break;
        case WIFI_EVENT_SCAN_DONE: {
            const wifi_event_sta_scan_done_t *scan = (const wifi_event_sta_scan_done_t *)event_data;
            if(scan != NULL) {
                ESP_LOGI(TAG, "Provisioning scan done (status: %u, ap_num: %u)",
                         (unsigned int)scan->status,
                         (unsigned int)scan->number);
            }
            break;
        }
        default:
            break;
        }
    } else if(event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Connected with IP: " IPSTR, IP2STR(&event->ip_info.ip));
        wifi_is_connected = true;
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_EVENT);
        wifi_start_time_sync();
    }
}

esp_err_t WifiProvisioning_Bootstrap(void)
{
    if(wifi_bootstrap_done) {
        return ESP_OK;
    }

    wifi_set_singapore_timezone();

    esp_err_t ret = nvs_flash_init();
    if(ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else if(ret != ESP_OK) {
        return ret;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_event_handler_register(NETWORK_PROV_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t wifi_init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_cfg));

    ESP_ERROR_CHECK(wifi_prov_manager_init_if_needed());

    bool provisioned = false;
    ESP_ERROR_CHECK(network_prov_mgr_is_wifi_provisioned(&provisioned));

    if(!provisioned) {
        ESP_ERROR_CHECK(wifi_start_softap_provisioning());
    } else {
        ESP_LOGI(TAG, "Already provisioned, starting Wi-Fi STA");
        ESP_ERROR_CHECK(network_prov_mgr_deinit());
        wifi_mgr_initialized = false;
        wifi_init_sta();
    }

    wifi_bootstrap_done = true;
    return ESP_OK;
}

esp_err_t WifiProvisioning_Reprovision(void)
{
    if(!wifi_bootstrap_done) {
        return ESP_ERR_INVALID_STATE;
    }

    if(wifi_is_provisioning) {
        return ESP_OK;
    }

    esp_err_t ret = wifi_prov_manager_init_if_needed();
    if(ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize provisioning manager for reprovisioning (error: %d)", ret);
        return ret;
    }

    ret = network_prov_mgr_reset_wifi_provisioning();
    if(ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reset Wi-Fi provisioning credentials (error: %d)", ret);
        return ret;
    }

    ret = esp_wifi_stop();
    if(ret != ESP_OK && ret != ESP_ERR_WIFI_NOT_STOPPED) {
        ESP_LOGE(TAG, "Failed to stop Wi-Fi before reprovisioning (error: %d)", ret);
        return ret;
    }

    return wifi_start_softap_provisioning();
}

bool WifiProvisioning_IsProvisioning(void)
{
    return wifi_is_provisioning;
}

void WifiProvisioning_GetConnectionStatus(char *buf, size_t len)
{
    if(buf == NULL || len == 0) {
        return;
    }

    if(!wifi_bootstrap_done) {
        strncpy(buf, "Wi-Fi: Initializing", len - 1);
    } else if(wifi_is_provisioning) {
        strncpy(buf, "Wi-Fi: Provisioning", len - 1);
    } else if(wifi_is_connected) {
        strncpy(buf, "Wi-Fi: Connected", len - 1);
    } else {
        strncpy(buf, "Wi-Fi: Connecting", len - 1);
    }

    buf[len - 1] = '\0';
}

void WifiProvisioning_GetServiceName(char *buf, size_t len)
{
    strncpy(buf, prov_service_name, len - 1);
    buf[len - 1] = '\0';
}

void WifiProvisioning_GetQRPayload(char *buf, size_t len)
{
    strncpy(buf, prov_qr_payload, len - 1);
    buf[len - 1] = '\0';
}