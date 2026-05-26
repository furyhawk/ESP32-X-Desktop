#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

#include <esp_event.h>
#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_netif_sntp.h>
#include <esp_random.h>
#include <esp_wifi.h>
#include <cJSON.h>
#include <lwip/sockets.h>
#include <lwip/inet.h>
#include <lwip/ip4_addr.h>
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
static httpd_handle_t prov_httpd_handle = NULL;
static esp_netif_t *wifi_ap_netif = NULL;
static TaskHandle_t captive_dns_task_handle = NULL;
static int captive_dns_sock = -1;
static esp_ip4_addr_t captive_dns_ip = {0};
static char prov_service_name[20] = {0};
static char prov_qr_payload[200] = {0};
static const char *SINGAPORE_TZ = "SGT-8";

#define CAPTIVE_DNS_PORT 53
#define CAPTIVE_DNS_MAX_PACKET 512

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

static int captive_dns_read_qname_end(const uint8_t *msg, int msg_len, int qname_offset)
{
    int idx = qname_offset;
    while(idx < msg_len) {
        uint8_t label_len = msg[idx++];
        if(label_len == 0) {
            return idx;
        }
        if((label_len & 0xC0) != 0 || (idx + label_len) > msg_len) {
            return -1;
        }
        idx += label_len;
    }
    return -1;
}

static void captive_dns_qname_to_string(const uint8_t *msg,
                                        int msg_len,
                                        int qname_offset,
                                        char *out,
                                        size_t out_len)
{
    if(out == NULL || out_len == 0) {
        return;
    }

    out[0] = '\0';
    int idx = qname_offset;
    size_t w = 0;

    while(idx < msg_len) {
        uint8_t label_len = msg[idx++];
        if(label_len == 0) {
            break;
        }
        if((label_len & 0xC0) != 0 || (idx + label_len) > msg_len) {
            break;
        }

        if(w > 0 && (w + 1) < out_len) {
            out[w++] = '.';
        }

        for(uint8_t i = 0; i < label_len && (w + 1) < out_len; ++i) {
            out[w++] = (char)msg[idx + i];
        }
        idx += label_len;
    }

    out[w] = '\0';
}

static int captive_dns_build_a_response(const uint8_t *query,
                                        int query_len,
                                        uint8_t *response,
                                        int response_cap,
                                        esp_ip4_addr_t ip)
{
    if(query_len < 12 || response_cap < 12) {
        return -1;
    }

    uint16_t flags = ((uint16_t)query[2] << 8) | query[3];
    uint16_t qdcount = ((uint16_t)query[4] << 8) | query[5];
    if((flags & 0x8000U) != 0 || qdcount == 0) {
        return -1;
    }

    int qname_end = captive_dns_read_qname_end(query, query_len, 12);
    if(qname_end < 0 || (qname_end + 4) > query_len) {
        return -1;
    }

    int question_len = (qname_end + 4) - 12;
    int answer_len = 16; /* name ptr + type + class + ttl + rdlen + ipv4 */
    int total_len = 12 + question_len + answer_len;
    if(total_len > response_cap) {
        return -1;
    }

    memcpy(response, query, 12 + question_len);
    response[2] = 0x81; /* QR=1, OPCODE=0, AA=0, TC=0, RD preserved */
    response[3] = 0x80; /* RA=1, RCODE=0 */
    response[4] = query[4];
    response[5] = query[5];
    response[6] = 0x00;
    response[7] = 0x01; /* one answer */
    response[8] = 0x00;
    response[9] = 0x00;
    response[10] = 0x00;
    response[11] = 0x00;

    int a = 12 + question_len;
    response[a++] = 0xC0;
    response[a++] = 0x0C; /* pointer to first question name */
    response[a++] = 0x00;
    response[a++] = 0x01; /* type A */
    response[a++] = 0x00;
    response[a++] = 0x01; /* class IN */
    response[a++] = 0x00;
    response[a++] = 0x00;
    response[a++] = 0x00;
    response[a++] = 0x3C; /* ttl 60s */
    response[a++] = 0x00;
    response[a++] = 0x04; /* ipv4 length */
    ip4_addr_t ip4 = {0};
    ip4.addr = ip.addr;
    response[a++] = ip4_addr1(&ip4);
    response[a++] = ip4_addr2(&ip4);
    response[a++] = ip4_addr3(&ip4);
    response[a++] = ip4_addr4(&ip4);

    return total_len;
}

static void captive_dns_server_task(void *arg)
{
    (void)arg;

    uint8_t rx_buf[CAPTIVE_DNS_MAX_PACKET] = {0};
    uint8_t tx_buf[CAPTIVE_DNS_MAX_PACKET] = {0};

    while(captive_dns_sock >= 0) {
        struct sockaddr_storage src_addr;
        memset(&src_addr, 0, sizeof(src_addr));
        socklen_t src_len = sizeof(src_addr);
        int rlen = recvfrom(captive_dns_sock, rx_buf, sizeof(rx_buf), 0,
                            (struct sockaddr *)&src_addr, &src_len);
        if(rlen <= 0) {
            continue;
        }

        char qname[96] = {0};
        captive_dns_qname_to_string(rx_buf, rlen, 12, qname, sizeof(qname));
        ESP_LOGI(TAG,
                 "Captive DNS query: host=%s (%d bytes) -> " IPSTR,
                 qname[0] != '\0' ? qname : "(unknown)",
                 rlen,
                 IP2STR(&captive_dns_ip));

        int tlen = captive_dns_build_a_response(rx_buf, rlen, tx_buf, sizeof(tx_buf), captive_dns_ip);
        if(tlen > 0) {
            sendto(captive_dns_sock, tx_buf, tlen, 0, (struct sockaddr *)&src_addr, src_len);
        }
    }

    captive_dns_task_handle = NULL;
    vTaskDelete(NULL);
}

static esp_err_t captive_dns_server_start(void)
{
    if(captive_dns_sock >= 0) {
        return ESP_OK;
    }

    if(wifi_ap_netif == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_netif_ip_info_t ap_ip;
    memset(&ap_ip, 0, sizeof(ap_ip));
    esp_err_t ret = esp_netif_get_ip_info(wifi_ap_netif, &ap_ip);
    if(ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read SoftAP IP for captive DNS (error: %d)", ret);
        return ret;
    }
    captive_dns_ip = ap_ip.ip;

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if(sock < 0) {
        ESP_LOGW(TAG, "Failed to create captive DNS socket");
        return ESP_FAIL;
    }

    struct sockaddr_in bind_addr;
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(CAPTIVE_DNS_PORT);
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if(bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) != 0) {
        ESP_LOGW(TAG, "Failed to bind captive DNS socket");
        close(sock);
        return ESP_FAIL;
    }

    captive_dns_sock = sock;

    BaseType_t task_ok = xTaskCreate(captive_dns_server_task,
                                     "captive_dns",
                                     4096,
                                     NULL,
                                     5,
                                     &captive_dns_task_handle);
    if(task_ok != pdPASS) {
        close(captive_dns_sock);
        captive_dns_sock = -1;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Captive DNS started on %s:53", ip4addr_ntoa((const ip4_addr_t *)&captive_dns_ip));
    return ESP_OK;
}

static void captive_dns_server_stop(void)
{
    if(captive_dns_sock >= 0) {
        close(captive_dns_sock);
        captive_dns_sock = -1;
    }
    ESP_LOGI(TAG, "Captive DNS stopped");
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
    uint32_t nonce = esp_random();
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    /* Use a per-run nonce suffix so Android does not silently auto-join a stale
     * remembered SSID and bypass the provisioning app network selection flow. */
    snprintf(service_name, max_len, "PROV_%02X%02X%02X%02X",
             mac[4], mac[5], (unsigned int)((nonce >> 8) & 0xFF), (unsigned int)(nonce & 0xFF));
}

static void wifi_prov_print_url(const char *name,
                                const char *username,
                                const char *pop,
                                int security,
                                const char *password)
{
    char password_field[96] = {0};
    if(password != NULL && password[0] != '\0') {
        snprintf(password_field, sizeof(password_field), ",\"password\":\"%s\"", password);
    }

    if(pop != NULL && pop[0] != '\0') {
        if(username != NULL && username[0] != '\0') {
            snprintf(prov_qr_payload, sizeof(prov_qr_payload),
                     "{\"ver\":\"%s\",\"name\":\"%s\",\"username\":\"%s\",\"pop\":\"%s\",\"transport\":\"%s\",\"security\":%d%s}",
                     PROV_QR_VERSION,
                     name,
                     username,
                     pop,
                     PROV_TRANSPORT_SOFTAP,
                     security,
                     password_field);
        } else {
            snprintf(prov_qr_payload, sizeof(prov_qr_payload),
                     "{\"ver\":\"%s\",\"name\":\"%s\",\"pop\":\"%s\",\"transport\":\"%s\",\"security\":%d%s}",
                     PROV_QR_VERSION,
                     name,
                     pop,
                     PROV_TRANSPORT_SOFTAP,
                     security,
                     password_field);
        }
    } else {
        snprintf(prov_qr_payload, sizeof(prov_qr_payload),
                 "{\"ver\":\"%s\",\"name\":\"%s\",\"transport\":\"%s\",\"security\":%d%s}",
                 PROV_QR_VERSION,
                 name,
                 PROV_TRANSPORT_SOFTAP,
                 security,
                 password_field);
    }

    ESP_LOGI(TAG, "Provisioning URL:\n%s?data=%s", QRCODE_BASE_URL, prov_qr_payload);
}

/* Respond to Android captive-portal probes (any GET) with HTTP 204 so Android
 * considers the SoftAP network valid and does not drop the connection before
 * the provisioning app has a chance to exchange credentials.
 *
 * For the manual fallback flow, return a redirect to /diag so the phone opens
 * the local credential page instead of silently treating the network as normal. */
static esp_err_t prov_captive_portal_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Captive portal probe intercepted: %s", req->uri);

    /* Android's connectivity service hits /generate_204 (and /gen_204) and
     * expects an exact HTTP 204 response.  Returning anything else (e.g. a
     * redirect) causes Android to classify the SoftAP as a captive portal and
     * force-disconnect the client after a short timeout — before the
     * provisioning app has a chance to exchange credentials.
     *
     * Apple's CNA hits /hotspot-detect.html and expects a 200 with "Success".
     *
     * For all other unknown probes we fall through to a /diag redirect so that
     * a manual browser-based flow still works. */
    const char *uri = req->uri;
    if(strstr(uri, "generate_204") != NULL || strstr(uri, "gen_204") != NULL) {
        httpd_resp_set_status(req, "204 No Content");
        return httpd_resp_send(req, NULL, 0);
    }

    if(strstr(uri, "hotspot-detect") != NULL || strstr(uri, "success.txt") != NULL
       || strstr(uri, "ncsi.txt") != NULL || strstr(uri, "connecttest") != NULL) {
        httpd_resp_set_type(req, "text/plain");
        return httpd_resp_sendstr(req, "Success");
    }

    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/diag");
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req,
                       "<html><head><meta http-equiv=\"refresh\" content=\"0; url=/diag\"></head>"
                       "<body>Open <a href=\"/diag\">/diag</a> to continue Wi-Fi setup.</body></html>");
    return ESP_OK;
}

static esp_err_t prov_diag_get_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Provisioning diag GET hit: %s", req->uri);
    static const char diag_html[] =
        "<!doctype html><html><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>ESP32 Wi-Fi Setup</title>"
        "<style>body{font-family:sans-serif;max-width:520px;margin:24px auto;padding:0 14px;}"
        "label{display:block;margin-top:12px;font-weight:600;}"
        "input{width:100%;padding:10px;margin-top:6px;box-sizing:border-box;}"
        "button{margin-top:16px;padding:10px 14px;}"
        "pre{background:#f3f3f3;padding:10px;white-space:pre-wrap;}"
        "</style></head><body>"
        "<h2>ESP32 Manual Wi-Fi Provisioning</h2>"
        "<p>If app provisioning fails, use this form.</p>"
        "<form action=\"/\" method=\"post\">"
        "<label>SSID<input name=\"ssid\" autocomplete=\"off\" required></label>"
        "<label>Password<input name=\"password\" type=\"password\" autocomplete=\"off\"></label>"
        "<button type=\"submit\">Save And Connect</button>"
        "</form>"
        "<p>Or POST JSON to <code>/</code> or <code>/diag</code> with <code>{&quot;ssid&quot;:&quot;...&quot;,&quot;password&quot;:&quot;...&quot;}</code>.</p>"
        "</body></html>";

    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, diag_html, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t wifi_apply_manual_credentials(const char *ssid, const char *password)
{
    if(ssid == NULL || ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    const size_t ssid_len = strnlen(ssid, sizeof(((wifi_sta_config_t *)0)->ssid));
    const size_t pass_len = (password != NULL) ? strnlen(password, sizeof(((wifi_sta_config_t *)0)->password)) : 0;
    if(ssid_len == 0 || ssid_len >= sizeof(((wifi_sta_config_t *)0)->ssid)) {
        return ESP_ERR_INVALID_ARG;
    }
    if(pass_len >= sizeof(((wifi_sta_config_t *)0)->password)) {
        return ESP_ERR_INVALID_ARG;
    }

    wifi_config_t sta_cfg = {0};
    memcpy(sta_cfg.sta.ssid, ssid, ssid_len);
    if(password != NULL && password[0] != '\0') {
        strlcpy((char *)sta_cfg.sta.password, password, sizeof(sta_cfg.sta.password));
    }

    esp_err_t ret = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if(ret != ESP_OK) {
        ESP_LOGE(TAG, "Manual credential apply: failed to set APSTA mode (err=%d)", ret);
        return ret;
    }

    ret = esp_wifi_set_storage(WIFI_STORAGE_FLASH);
    if(ret != ESP_OK) {
        ESP_LOGE(TAG, "Manual credential apply: failed to set Wi-Fi storage (err=%d)", ret);
        return ret;
    }

    ret = esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);
    if(ret != ESP_OK) {
        ESP_LOGE(TAG, "Manual credential apply: failed to set STA config (err=%d)", ret);
        return ret;
    }

    /* Manual provisioning fallback bypasses protocomm flow, so enable normal STA
     * reconnect behavior immediately. */
    wifi_is_provisioning = false;

    ret = esp_wifi_connect();
    if(ret != ESP_OK) {
        ESP_LOGE(TAG, "Manual credential apply: failed to trigger STA connect (err=%d)", ret);
        return ret;
    }

    ESP_LOGI(TAG, "Manual credential apply accepted for SSID: %s", ssid);
    return ESP_OK;
}

static bool diag_parse_form_field(const char *body, const char *key, char *out, size_t out_len)
{
    if(body == NULL || key == NULL || out == NULL || out_len == 0) {
        return false;
    }

    const size_t key_len = strlen(key);
    const char *match = strstr(body, key);
    if(match == NULL || match[key_len] != '=') {
        return false;
    }

    match += key_len + 1;
    size_t w = 0;
    while(*match != '\0' && *match != '&' && w + 1 < out_len) {
        if(*match == '+') {
            out[w++] = ' ';
        } else if(*match == '%' && isxdigit((unsigned char)match[1]) && isxdigit((unsigned char)match[2])) {
            char hex[3] = { match[1], match[2], '\0' };
            out[w++] = (char)strtol(hex, NULL, 16);
            match += 2;
        } else {
            out[w++] = *match;
        }
        match++;
    }
    out[w] = '\0';
    return w > 0 || (key_len > 0 && strcmp(key, "password") == 0);
}

static esp_err_t prov_diag_post_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Provisioning diag POST hit: %s (len=%d)", req->uri, req->content_len);

    if(req->content_len <= 0 || req->content_len > 512) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"invalid_content_length\"}");
    }

    char body[513] = {0};
    int total_read = 0;
    while(total_read < req->content_len) {
        int r = httpd_req_recv(req, body + total_read, req->content_len - total_read);
        if(r <= 0) {
            httpd_resp_set_status(req, "408 Request Timeout");
            httpd_resp_set_type(req, "application/json");
            return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"recv_failed\"}");
        }
        total_read += r;
    }
    body[total_read] = '\0';

    char ssid[33] = {0};
    char password[65] = {0};
    const char *ssid_ptr = NULL;
    const char *password_ptr = NULL;

    cJSON *root = cJSON_Parse(body);
    if(root != NULL) {
        const cJSON *ssid_item = cJSON_GetObjectItemCaseSensitive(root, "ssid");
        const cJSON *password_item = cJSON_GetObjectItemCaseSensitive(root, "password");
        if(password_item == NULL) {
            password_item = cJSON_GetObjectItemCaseSensitive(root, "passphrase");
        }
        if(cJSON_IsString(ssid_item)) {
            ssid_ptr = ssid_item->valuestring;
        }
        if(cJSON_IsString(password_item)) {
            password_ptr = password_item->valuestring;
        }
        cJSON_Delete(root);
    }

    if(ssid_ptr == NULL) {
        if(!diag_parse_form_field(body, "ssid", ssid, sizeof(ssid))) {
            httpd_resp_set_status(req, "400 Bad Request");
            httpd_resp_set_type(req, "application/json");
            return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"missing_ssid\"}");
        }
        ssid_ptr = ssid;
    }
    if(password_ptr == NULL) {
        if(diag_parse_form_field(body, "password", password, sizeof(password))) {
            password_ptr = password;
        } else if(diag_parse_form_field(body, "passphrase", password, sizeof(password))) {
            password_ptr = password;
        } else {
            password_ptr = "";
        }
    }

    esp_err_t apply_ret = wifi_apply_manual_credentials(ssid_ptr, password_ptr);

    if(apply_ret != ESP_OK) {
        char resp[96] = {0};
        snprintf(resp, sizeof(resp), "{\"ok\":false,\"error\":\"apply_failed\",\"code\":%d}", (int)apply_ret);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, resp);
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true,\"mode\":\"manual_wifi_apply\"}");
}

static esp_err_t prov_httpd_err_handler(httpd_req_t *req, httpd_err_code_t error)
{
    if(req != NULL) {
        ESP_LOGW(TAG, "Provisioning HTTP miss (method=%d, uri=%s, err=%d)",
                 (int)req->method,
                 req->uri != NULL ? req->uri : "(null)",
                 (int)error);
    } else {
        ESP_LOGW(TAG, "Provisioning HTTP miss (null req, err=%d)", (int)error);
    }

    if(req != NULL && (error == HTTPD_404_NOT_FOUND || error == HTTPD_405_METHOD_NOT_ALLOWED)) {
        return httpd_resp_send_err(req, error, NULL);
    }
    return ESP_FAIL;
}

static const httpd_uri_t prov_root_get_uri = {
    .uri      = "/",
    .method   = HTTP_GET,
    .handler  = prov_diag_get_handler,
    .user_ctx = NULL
};

static const httpd_uri_t prov_root_post_uri = {
    .uri      = "/",
    .method   = HTTP_POST,
    .handler  = prov_diag_post_handler,
    .user_ctx = NULL
};

static const httpd_uri_t prov_captive_portal_uri = {
    .uri      = "/*",          /* wildcard: catches /generate_204 and friends */
    .method   = HTTP_GET,
    .handler  = prov_captive_portal_handler,
    .user_ctx = NULL
};

static const httpd_uri_t prov_diag_get_uri = {
    .uri      = "/diag",
    .method   = HTTP_GET,
    .handler  = prov_diag_get_handler,
    .user_ctx = NULL
};

static const httpd_uri_t prov_diag_post_uri = {
    .uri      = "/diag",
    .method   = HTTP_POST,
    .handler  = prov_diag_post_handler,
    .user_ctx = NULL
};

static esp_err_t wifi_prov_manager_init_if_needed(void)
{
    if(wifi_mgr_initialized) {
        return ESP_OK;
    }

    if(prov_httpd_handle == NULL) {
        httpd_config_t httpd_cfg = HTTPD_DEFAULT_CONFIG();
        /* Some Android stacks pause reads/writes during network handoff checks. */
        httpd_cfg.recv_wait_timeout = 30;
        httpd_cfg.send_wait_timeout = 30;
        /* Captive-portal/captive-browser traffic may include large cookie and
         * telemetry headers; default 512 bytes can trigger HTTP 431. */
        httpd_cfg.max_req_hdr_len = 2048;
        httpd_cfg.max_uri_len = 512;
        httpd_cfg.lru_purge_enable = true;
        /* Provisioning registers ~5 POST endpoints; +1 for our wildcard GET. */
        httpd_cfg.max_uri_handlers = 10;
        /* Required for wildcard URI matching ("slash-star") to work; provisioning
         * POST endpoints are exact matches and still take priority. */
        httpd_cfg.uri_match_fn = httpd_uri_match_wildcard;
        esp_err_t httpd_ret = httpd_start(&prov_httpd_handle, &httpd_cfg);
        if(httpd_ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start provisioning HTTPD (error: %d)", httpd_ret);
            return httpd_ret;
        }

        httpd_ret = httpd_register_uri_handler(prov_httpd_handle, &prov_diag_get_uri);
        if(httpd_ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register /diag GET handler (error: %d)", httpd_ret);
            httpd_stop(prov_httpd_handle);
            prov_httpd_handle = NULL;
            return httpd_ret;
        }
        ESP_LOGI(TAG, "Provisioning diag endpoint registered: GET /diag");

        httpd_ret = httpd_register_uri_handler(prov_httpd_handle, &prov_diag_post_uri);
        if(httpd_ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register /diag POST handler (error: %d)", httpd_ret);
            httpd_stop(prov_httpd_handle);
            prov_httpd_handle = NULL;
            return httpd_ret;
        }
        ESP_LOGI(TAG, "Provisioning diag endpoint registered: POST /diag");

        httpd_ret = httpd_register_uri_handler(prov_httpd_handle, &prov_root_get_uri);
        if(httpd_ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register / GET handler (error: %d)", httpd_ret);
            httpd_stop(prov_httpd_handle);
            prov_httpd_handle = NULL;
            return httpd_ret;
        }
        ESP_LOGI(TAG, "Provisioning root endpoint registered: GET /");

        httpd_ret = httpd_register_uri_handler(prov_httpd_handle, &prov_root_post_uri);
        if(httpd_ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register / POST handler (error: %d)", httpd_ret);
            httpd_stop(prov_httpd_handle);
            prov_httpd_handle = NULL;
            return httpd_ret;
        }
        ESP_LOGI(TAG, "Provisioning root endpoint registered: POST /");

        /* Register wildcard GET handler for Android captive-portal detection. */
        httpd_ret = httpd_register_uri_handler(prov_httpd_handle, &prov_captive_portal_uri);
        if(httpd_ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register captive-portal handler (error: %d)", httpd_ret);
            httpd_stop(prov_httpd_handle);
            prov_httpd_handle = NULL;
            return httpd_ret;
        }

        httpd_ret = httpd_register_err_handler(prov_httpd_handle, HTTPD_404_NOT_FOUND, prov_httpd_err_handler);
        if(httpd_ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register HTTP 404 handler (error: %d)", httpd_ret);
            httpd_stop(prov_httpd_handle);
            prov_httpd_handle = NULL;
            return httpd_ret;
        }

        httpd_ret = httpd_register_err_handler(prov_httpd_handle, HTTPD_405_METHOD_NOT_ALLOWED, prov_httpd_err_handler);
        if(httpd_ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register HTTP 405 handler (error: %d)", httpd_ret);
            httpd_stop(prov_httpd_handle);
            prov_httpd_handle = NULL;
            return httpd_ret;
        }

        ESP_LOGI(TAG, "Captive-portal wildcard handler registered");
    }

    /* protocomm expects a pointer to httpd_handle_t storage, not the raw handle value. */
    network_prov_scheme_softap_set_httpd_handle((void *)&prov_httpd_handle);

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
    int security_ver = 0;
    const char *pop = "abcd1234";
    const char *service_key = NULL;
    const char *username = NULL;
    const char *security_label = NULL;
    const char *ap_auth_label = "open";
    network_prov_security_t security;
    const void *security_params;

#if CONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_VERSION_1
    security = NETWORK_PROV_SECURITY_1;
    security_ver = 1;
    security_params = (const void *)pop;
    security_label = "sec1";
#elif CONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_VERSION_2
    network_prov_security2_params_t sec2_params = {};
    sec2_params.salt = sec2_salt;
    sec2_params.salt_len = sizeof(sec2_salt);
    sec2_params.verifier = sec2_verifier;
    sec2_params.verifier_len = sizeof(sec2_verifier);
    security = NETWORK_PROV_SECURITY_2;
    security_ver = 2;
    security_params = (const void *)&sec2_params;
    username = "wifiprov";
    security_label = "sec2";
#elif CONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_VERSION_0
    security = NETWORK_PROV_SECURITY_0;
    security_ver = 0;
    security_params = NULL;
    security_label = "sec0";
    pop = NULL;
#else
#error "At least one protocomm security version must be enabled"
#endif

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

    if(service_key == NULL || service_key[0] == '\0') {
        ap_auth_label = "open";
    }

    ESP_LOGI(TAG, "Starting SoftAP provisioning (service name: %s, auth: %s + %s)",
             service_name,
             ap_auth_label,
             security_label);
    esp_err_t ret = network_prov_mgr_start_provisioning(security,
                                                        security_params,
                                                        service_name,
                                                        service_key);
    if(ret != ESP_OK) {
        wifi_is_provisioning = false;
        return ret;
    }

    ret = captive_dns_server_start();
    if(ret != ESP_OK) {
        ESP_LOGW(TAG, "Captive DNS did not start (error: %d)", ret);
    }

    ESP_LOGI(TAG, "Diag URL (while connected to SoftAP): http://192.168.4.1/diag");

    wifi_prov_print_url(service_name, username, pop, security_ver, service_key);
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
            captive_dns_server_stop();
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
            if(prov_httpd_handle != NULL) {
                esp_err_t httpd_ret = httpd_stop(prov_httpd_handle);
                if(httpd_ret != ESP_OK) {
                    ESP_LOGW(TAG, "Failed to stop provisioning HTTPD (error: %d)", httpd_ret);
                }
                prov_httpd_handle = NULL;
            }
            break;
        default:
            break;
        }
    } else if(event_base == PROTOCOMM_SECURITY_SESSION_EVENT) {
        switch(event_id) {
        case PROTOCOMM_SECURITY_SESSION_SETUP_OK:
            ESP_LOGI(TAG, "Provisioning secure session established");
            break;
        case PROTOCOMM_SECURITY_SESSION_INVALID_SECURITY_PARAMS:
            ESP_LOGE(TAG, "Provisioning secure session failed: invalid security params");
            break;
        case PROTOCOMM_SECURITY_SESSION_CREDENTIALS_MISMATCH:
            ESP_LOGE(TAG, "Provisioning secure session failed: incorrect PoP/credentials");
            break;
        default:
            ESP_LOGW(TAG, "Provisioning secure session event id: %ld", (long)event_id);
            break;
        }
    } else if(event_base == WIFI_EVENT) {
        switch(event_id) {
        case WIFI_EVENT_AP_START:
            ESP_LOGI(TAG, "Provisioning SoftAP started");
            if(wifi_ap_netif != NULL) {
                esp_err_t dhcp_ret = esp_netif_dhcps_start(wifi_ap_netif);
                if(dhcp_ret == ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED) {
                    ESP_LOGI(TAG, "SoftAP DHCP server already running");
                } else if(dhcp_ret != ESP_OK) {
                    ESP_LOGW(TAG, "SoftAP DHCP server start failed (error: %d)", dhcp_ret);
                } else {
                    ESP_LOGI(TAG, "SoftAP DHCP server started/restarted on AP start event");
                }
            }
            break;
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
            if(event_data != NULL) {
                const wifi_event_ap_staconnected_t *ap_conn = (const wifi_event_ap_staconnected_t *)event_data;
                ESP_LOGI(TAG,
                         "Provisioning SoftAP client connected: %02x:%02x:%02x:%02x:%02x:%02x (AID=%u)",
                         ap_conn->mac[0], ap_conn->mac[1], ap_conn->mac[2],
                         ap_conn->mac[3], ap_conn->mac[4], ap_conn->mac[5],
                         (unsigned int)ap_conn->aid);
            } else {
                ESP_LOGI(TAG, "Provisioning SoftAP client connected");
            }

            if(wifi_ap_netif != NULL) {
                esp_err_t dhcp_ret = esp_netif_dhcps_start(wifi_ap_netif);
                if(dhcp_ret == ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED) {
                    ESP_LOGI(TAG, "SoftAP DHCP already running at client connect");
                } else if(dhcp_ret == ESP_OK) {
                    ESP_LOGI(TAG, "SoftAP DHCP restarted at client connect");
                } else {
                    ESP_LOGW(TAG, "SoftAP DHCP start failed at client connect (error: %d)", dhcp_ret);
                }
            }
            break;
        case WIFI_EVENT_AP_STADISCONNECTED:
            if(event_data != NULL) {
                const wifi_event_ap_stadisconnected_t *ap_disc = (const wifi_event_ap_stadisconnected_t *)event_data;
                ESP_LOGI(TAG,
                         "Provisioning SoftAP client disconnected: %02x:%02x:%02x:%02x:%02x:%02x (AID=%u, reason=%u)",
                         ap_disc->mac[0], ap_disc->mac[1], ap_disc->mac[2],
                         ap_disc->mac[3], ap_disc->mac[4], ap_disc->mac[5],
                         (unsigned int)ap_disc->aid,
                         (unsigned int)ap_disc->reason);
            } else {
                ESP_LOGI(TAG, "Provisioning SoftAP client disconnected");
            }
            break;
        case WIFI_EVENT_AP_WRONG_PASSWORD:
            ESP_LOGW(TAG, "Provisioning SoftAP auth failure: station used wrong password");
            break;
        case WIFI_EVENT_HOME_CHANNEL_CHANGE:
            ESP_LOGI(TAG, "Provisioning SoftAP home channel changed");
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
            ESP_LOGI(TAG, "WIFI_EVENT id: %ld", (long)event_id);
            break;
        }
    } else if(event_base == IP_EVENT) {
        switch(event_id) {
        case IP_EVENT_STA_GOT_IP: {
            ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
            ESP_LOGI(TAG, "Connected with IP: " IPSTR, IP2STR(&event->ip_info.ip));
            wifi_is_connected = true;
            xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_EVENT);
            wifi_start_time_sync();
            break;
        }
        case IP_EVENT_ASSIGNED_IP_TO_CLIENT: {
            const ip_event_assigned_ip_to_client_t *event = (const ip_event_assigned_ip_to_client_t *)event_data;
            ESP_LOGI(TAG,
                     "SoftAP DHCP lease: client %02x:%02x:%02x:%02x:%02x:%02x -> " IPSTR,
                     event->mac[0], event->mac[1], event->mac[2], event->mac[3], event->mac[4], event->mac[5],
                     IP2STR(&event->ip));
            break;
        }
        default:
            break;
        }
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
    ESP_ERROR_CHECK(esp_event_handler_register(PROTOCOMM_SECURITY_SESSION_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ASSIGNED_IP_TO_CLIENT, &event_handler, NULL));

    esp_netif_create_default_wifi_sta();
    wifi_ap_netif = esp_netif_create_default_wifi_ap();

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

esp_err_t WifiProvisioning_Cancel(void)
{
    if(!wifi_bootstrap_done) {
        return ESP_ERR_INVALID_STATE;
    }

    if(!wifi_is_provisioning) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Cancelling active Wi-Fi provisioning");

    /* Stop protocomm provisioning transport asynchronously. NETWORK_PROV_END will
     * follow and finish manager teardown. */
    network_prov_mgr_stop_provisioning();

    /* Update state immediately so UI can dismiss provisioning overlay right away. */
    wifi_is_provisioning = false;
    wifi_is_connected = false;
    if(wifi_event_group != NULL) {
        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_EVENT);
    }

    return ESP_OK;
}

bool WifiProvisioning_IsProvisioning(void)
{
    return wifi_is_provisioning;
}

bool WifiProvisioning_IsConnected(void)
{
    return wifi_is_connected && !wifi_is_provisioning;
}

bool WifiProvisioning_IsSystemTimeSynchronized(void)
{
    const time_t minimum_valid_epoch = 1704067200; /* 2024-01-01 00:00:00 UTC */
    time_t now = time(NULL);
    return now >= minimum_valid_epoch;
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