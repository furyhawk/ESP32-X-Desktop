/*
 * SPDX-FileCopyrightText: 2019-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <esp_log.h>
#include <string.h>
#include <esp_err.h>
#include <esp_wifi.h>

#include "network_scan.pb-c.h"

#include <network_provisioning/network_scan.h>

static const char *TAG = "proto_network_scan";

typedef struct network_prov_scan_cmd {
    int cmd_num;
    esp_err_t (*command_handler)(NetworkScanPayload *req,
                                 NetworkScanPayload *resp, void *priv_data);
} network_prov_scan_cmd_t;

static esp_err_t cmd_scan_start_handler(NetworkScanPayload *req,
                                        NetworkScanPayload *resp,
                                        void *priv_data);

static esp_err_t cmd_scan_status_handler(NetworkScanPayload *req,
        NetworkScanPayload *resp,
        void *priv_data);

static esp_err_t cmd_scan_result_handler(NetworkScanPayload *req,
        NetworkScanPayload *resp,
        void *priv_data);

static network_prov_scan_cmd_t cmd_table[] = {
    {
        .cmd_num = NETWORK_SCAN_MSG_TYPE__TypeCmdScanWifiStart,
        .command_handler = cmd_scan_start_handler
    },
    {
        .cmd_num = NETWORK_SCAN_MSG_TYPE__TypeCmdScanWifiStatus,
        .command_handler = cmd_scan_status_handler
    },
    {
        .cmd_num = NETWORK_SCAN_MSG_TYPE__TypeCmdScanWifiResult,
        .command_handler = cmd_scan_result_handler
    },
    {
        .cmd_num = NETWORK_SCAN_MSG_TYPE__TypeCmdScanThreadStart,
        .command_handler = cmd_scan_start_handler
    },
    {
        .cmd_num = NETWORK_SCAN_MSG_TYPE__TypeCmdScanThreadStatus,
        .command_handler = cmd_scan_status_handler
    },
    {
        .cmd_num = NETWORK_SCAN_MSG_TYPE__TypeCmdScanThreadResult,
        .command_handler = cmd_scan_result_handler
    }
};

static size_t sanitize_ssid_for_client(const char *src, char *dst, size_t dst_len)
{
    if (!src || !dst || dst_len == 0) {
        return 0;
    }

    size_t src_len = strnlen(src, 32);
    size_t out = 0;
    for (size_t i = 0; i < src_len && out < (dst_len - 1); i++) {
        unsigned char c = (unsigned char)src[i];
        /* Keep returned SSID bytes ASCII-safe to avoid client UTF-8 decode failures. */
        if (c >= 0x20 && c <= 0x7E) {
            dst[out++] = (char)c;
        } else {
            dst[out++] = '?';
        }
    }

    dst[out] = '\0';
    return out;
}

static uint32_t map_auth_for_client(uint8_t authmode)
{
    switch (authmode) {
    case WIFI_AUTH_OPEN:
        return WIFI_AUTH_MODE__Open;
    case WIFI_AUTH_WEP:
        return WIFI_AUTH_MODE__WEP;
    case WIFI_AUTH_WPA_PSK:
        return WIFI_AUTH_MODE__WPA_PSK;
    case WIFI_AUTH_WPA2_PSK:
        return WIFI_AUTH_MODE__WPA2_PSK;
    case WIFI_AUTH_WPA_WPA2_PSK:
        return WIFI_AUTH_MODE__WPA_WPA2_PSK;
    case WIFI_AUTH_WPA2_ENTERPRISE:
        return WIFI_AUTH_MODE__WPA2_ENTERPRISE;
#ifdef WIFI_AUTH_WPA3_PSK
    case WIFI_AUTH_WPA3_PSK:
        return WIFI_AUTH_MODE__WPA3_PSK;
#endif
#ifdef WIFI_AUTH_WPA2_WPA3_PSK
    case WIFI_AUTH_WPA2_WPA3_PSK:
        return WIFI_AUTH_MODE__WPA2_WPA3_PSK;
#endif
    default:
        /* Keep a known enum value for older clients when AP auth mode is newer/unknown. */
        return WIFI_AUTH_MODE__WPA2_PSK;
    }
}

static esp_err_t cmd_scan_start_handler(NetworkScanPayload *req,
                                        NetworkScanPayload *resp, void *priv_data)
{
    network_prov_scan_handlers_t *h = (network_prov_scan_handlers_t *) priv_data;
    if (!h) {
        ESP_LOGE(TAG, "Command invoked without handlers");
        return ESP_ERR_INVALID_STATE;
    }
    if (req->msg == NETWORK_SCAN_MSG_TYPE__TypeCmdScanWifiStart) {
        RespScanWifiStart *resp_payload = (RespScanWifiStart *) malloc(sizeof(RespScanWifiStart));
        if (!resp_payload) {
            ESP_LOGE(TAG, "Error allocating memory");
            return ESP_ERR_NO_MEM;
        }
        resp_scan_wifi_start__init(resp_payload);

        bool blocking = false;
        bool passive = false;
        uint8_t group_channels = 4;
        uint32_t period_ms = 120;

        if (req->payload_case != NETWORK_SCAN_PAYLOAD__PAYLOAD_CMD_SCAN_WIFI_START || !req->cmd_scan_wifi_start) {
            ESP_LOGW(TAG, "Invalid WiFi scan start command payload; using safe defaults");
        } else {
            blocking = req->cmd_scan_wifi_start->blocking;
            passive = req->cmd_scan_wifi_start->passive;
            group_channels = req->cmd_scan_wifi_start->group_channels;
            period_ms = req->cmd_scan_wifi_start->period_ms;
        }

#ifdef CONFIG_NETWORK_PROV_NETWORK_TYPE_WIFI
        if (h->wifi_scan_start) {
            resp->status = (h->wifi_scan_start(blocking,
                                               passive,
                                               group_channels,
                                               period_ms,
                                               &h->ctx) == ESP_OK ? STATUS__Success : STATUS__InternalError);
        } else {
            resp->status = STATUS__InternalError;
        }
#else
        resp->status = STATUS__InvalidArgument;
#endif
        resp->payload_case = NETWORK_SCAN_PAYLOAD__PAYLOAD_RESP_SCAN_WIFI_START;
        resp->resp_scan_wifi_start = resp_payload;
    } else if (req->msg == NETWORK_SCAN_MSG_TYPE__TypeCmdScanThreadStart) {
        RespScanThreadStart *resp_payload = (RespScanThreadStart *) malloc(sizeof(RespScanThreadStart));
        if (!resp_payload) {
            ESP_LOGE(TAG, "Error allocating memory");
            return ESP_ERR_NO_MEM;
        }
        resp_scan_thread_start__init(resp_payload);

        if (req->payload_case != NETWORK_SCAN_PAYLOAD__PAYLOAD_CMD_SCAN_THREAD_START || !req->cmd_scan_thread_start) {
            ESP_LOGE(TAG, "Invalid Thread scan start command");
            resp->resp_scan_thread_start = resp_payload;
            return ESP_ERR_INVALID_ARG;
        }

#ifdef CONFIG_NETWORK_PROV_NETWORK_TYPE_THREAD
        if (h->thread_scan_start) {
            resp->status = (h->thread_scan_start(req->cmd_scan_thread_start->blocking,
                                                 req->cmd_scan_thread_start->channel_mask,
                                                 &h->ctx) == ESP_OK ? STATUS__Success : STATUS__InternalError);
        } else {
            resp->status = STATUS__InternalError;
        }
#else
        resp->status = STATUS__InvalidArgument;
#endif
        resp->payload_case = NETWORK_SCAN_PAYLOAD__PAYLOAD_RESP_SCAN_THREAD_START;
        resp->resp_scan_thread_start = resp_payload;
    }
    return ESP_OK;
}

static esp_err_t cmd_scan_status_handler(NetworkScanPayload *req,
        NetworkScanPayload *resp, void *priv_data)
{
    bool scan_finished = false;
    uint16_t result_count = 0;

    network_prov_scan_handlers_t *h = (network_prov_scan_handlers_t *) priv_data;
    if (!h) {
        ESP_LOGE(TAG, "Command invoked without handlers");
        return ESP_ERR_INVALID_STATE;
    }
    if (req->msg == NETWORK_SCAN_MSG_TYPE__TypeCmdScanWifiStatus) {
        RespScanWifiStatus *resp_payload = (RespScanWifiStatus *) malloc(sizeof(RespScanWifiStatus));
        if (!resp_payload) {
            ESP_LOGE(TAG, "Error allocating memory");
            return ESP_ERR_NO_MEM;
        }
        resp_scan_wifi_status__init(resp_payload);
#ifdef CONFIG_NETWORK_PROV_NETWORK_TYPE_WIFI
        if (h->wifi_scan_status) {
            resp->status = (h->wifi_scan_status(&scan_finished, &result_count, &h->ctx) == ESP_OK ?
                            STATUS__Success : STATUS__InternalError);
        } else {
            resp->status = STATUS__InternalError;
        }
#else
        resp->status = STATUS__InvalidArgument;
#endif
        resp_payload->scan_finished = scan_finished;
        resp_payload->result_count = result_count;
        resp->payload_case = NETWORK_SCAN_PAYLOAD__PAYLOAD_RESP_SCAN_WIFI_STATUS;
        resp->resp_scan_wifi_status = resp_payload;
    } else if (req->msg == NETWORK_SCAN_MSG_TYPE__TypeCmdScanThreadStatus) {
        RespScanThreadStatus *resp_payload = (RespScanThreadStatus *) malloc(sizeof(RespScanThreadStatus));
        if (!resp_payload) {
            ESP_LOGE(TAG, "Error allocating memory");
            return ESP_ERR_NO_MEM;
        }
        resp_scan_thread_status__init(resp_payload);
#ifdef CONFIG_NETWORK_PROV_NETWORK_TYPE_THREAD
        if (h->thread_scan_status) {
            resp->status = (h->thread_scan_status(&scan_finished, &result_count, &h->ctx) == ESP_OK ?
                            STATUS__Success : STATUS__InternalError);
        } else {
            resp->status = STATUS__InternalError;
        }
#else
        resp->status = STATUS__InvalidArgument;
#endif
        resp_payload->scan_finished = scan_finished;
        resp_payload->result_count = result_count;
        resp->payload_case = NETWORK_SCAN_PAYLOAD__PAYLOAD_RESP_SCAN_THREAD_STATUS;
        resp->resp_scan_thread_status = resp_payload;
    }
    return ESP_OK;
}

static esp_err_t cmd_scan_result_handler(NetworkScanPayload *req,
        NetworkScanPayload *resp, void *priv_data)
{
    esp_err_t err = ESP_OK;
    network_prov_scan_handlers_t *h = (network_prov_scan_handlers_t *) priv_data;
    if (!h) {
        ESP_LOGE(TAG, "Command invoked without handlers");
        return ESP_ERR_INVALID_STATE;
    }
    if (req->msg == NETWORK_SCAN_MSG_TYPE__TypeCmdScanWifiResult) {
        RespScanWifiResult *resp_payload = (RespScanWifiResult *) malloc(sizeof(RespScanWifiResult));
        if (!resp_payload) {
            ESP_LOGE(TAG, "Error allocating memory");
            return ESP_ERR_NO_MEM;
        }
        resp_scan_wifi_result__init(resp_payload);

        uint16_t start_index = 0;
        uint16_t req_wifi_count = 4;

        if (req->payload_case != NETWORK_SCAN_PAYLOAD__PAYLOAD_CMD_SCAN_WIFI_RESULT || !req->cmd_scan_wifi_result) {
            ESP_LOGW(TAG, "Invalid WiFi scan result command payload; using defaults start=0 count=4");
        } else {
            start_index = req->cmd_scan_wifi_result->start_index;
            req_wifi_count = req->cmd_scan_wifi_result->count;
        }

        if (start_index >= CONFIG_NETWORK_PROV_SCAN_MAX_ENTRIES) {
            ESP_LOGE(TAG, "WiFi scan result count/start_index out of bounds");
            resp->resp_scan_wifi_result = resp_payload;
            return ESP_ERR_INVALID_ARG;
        }

        uint16_t max_wifi_count = CONFIG_NETWORK_PROV_SCAN_MAX_ENTRIES - start_index;
        uint16_t effective_wifi_count = MIN(req_wifi_count, max_wifi_count);
        ESP_LOGI(TAG, "WiFi scan result requested: start=%u count=%u (effective=%u)",
                 start_index,
                 req_wifi_count,
                 effective_wifi_count);
        if (req_wifi_count != effective_wifi_count) {
            ESP_LOGW(TAG, "Clamping WiFi scan result count from %u to %u (start_index=%u)",
                     req_wifi_count, effective_wifi_count, start_index);
        }

        resp->status = STATUS__Success;
        resp->payload_case = NETWORK_SCAN_PAYLOAD__PAYLOAD_RESP_SCAN_WIFI_RESULT;
        resp->resp_scan_wifi_result = resp_payload;
#ifdef CONFIG_NETWORK_PROV_NETWORK_TYPE_WIFI
        network_prov_scan_wifi_result_t scan_result = {{0}, {0}, 0, 0, 0};
        WiFiScanResult **results = NULL;

        /* Allocate memory only if there are non-zero scan results */
        if (effective_wifi_count) {
            results = (WiFiScanResult **) calloc(effective_wifi_count,
                                                 sizeof(WiFiScanResult *));
            if (!results) {
                ESP_LOGE(TAG, "Failed to allocate memory for results array");
                return ESP_ERR_NO_MEM;
            }
        }
        resp_payload->entries = results;
        resp_payload->n_entries = effective_wifi_count;

        /* If req->cmd_scan_wifi_result->count is 0, the below loop will
        * be skipped.
        */
        for (uint32_t i = 0; i < effective_wifi_count; i++) {
            if (!h->wifi_scan_result) {
                resp_payload->n_entries = i;
                resp->status = STATUS__InternalError;
                break;
            }
            /* start_index and count are validated above to sum to at most
             * CONFIG_NETWORK_PROV_SCAN_MAX_ENTRIES (max 255), so this cast is safe. */
            uint16_t result_index = (uint16_t)(i + start_index);
            err = h->wifi_scan_result(result_index, &scan_result, &h->ctx);
            if (err != ESP_OK) {
                resp_payload->n_entries = i;
                resp->status = STATUS__InternalError;
                break;
            }

            results[i] = (WiFiScanResult *) malloc(sizeof(WiFiScanResult));
            if (!results[i]) {
                ESP_LOGE(TAG, "Failed to allocate memory for result entry");
                resp_payload->n_entries = i;
                resp->status = STATUS__InternalError;
                return ESP_ERR_NO_MEM;
            }
            wi_fi_scan_result__init(results[i]);

            char ssid_sanitized[33] = {0};
            size_t ssid_len = sanitize_ssid_for_client(scan_result.ssid, ssid_sanitized, sizeof(ssid_sanitized));
            results[i]->ssid.len = ssid_len;
            results[i]->ssid.data = (uint8_t *) strdup(ssid_sanitized);
            if (!results[i]->ssid.data) {
                ESP_LOGE(TAG, "Failed to allocate memory for scan result entry SSID");
                results[i]->ssid.len = 0;
                resp_payload->n_entries = i + 1;
                resp->status = STATUS__InternalError;
                return ESP_ERR_NO_MEM;
            }

            results[i]->channel = scan_result.channel;
            results[i]->rssi = scan_result.rssi;
            results[i]->auth = map_auth_for_client(scan_result.auth);

            results[i]->bssid.len = sizeof(scan_result.bssid);
            results[i]->bssid.data = malloc(results[i]->bssid.len);
            if (!results[i]->bssid.data) {
                ESP_LOGE(TAG, "Failed to allocate memory for scan result entry BSSID");
                results[i]->bssid.len = 0;
                resp_payload->n_entries = i + 1;
                resp->status = STATUS__InternalError;
                return ESP_ERR_NO_MEM;
            }
            memcpy(results[i]->bssid.data, scan_result.bssid, results[i]->bssid.len);

            ESP_LOGI(TAG,
                     "WiFi scan entry[%u]: ssid='%s' ch=%u rssi=%d auth_raw=%u auth_sent=%u",
                     (unsigned int)i,
                     ssid_sanitized,
                     (unsigned int)results[i]->channel,
                     (int)results[i]->rssi,
                     (unsigned int)scan_result.auth,
                     (unsigned int)results[i]->auth);
        }
            ESP_LOGI(TAG, "WiFi scan result responded: entries=%u status=%d",
                 (unsigned int)resp_payload->n_entries,
                 (int)resp->status);
#else // CONFIG_NETWORK_PROV_NETWORK_TYPE_WIFI
        resp->status = STATUS__InvalidArgument;
#endif // !CONFIG_NETWORK_PROV_NETWORK_TYPE_WIFI
    } else if (req->msg == NETWORK_SCAN_MSG_TYPE__TypeCmdScanThreadResult) {
        RespScanThreadResult *resp_payload = (RespScanThreadResult *) malloc(sizeof(RespScanThreadResult));
        if (!resp_payload) {
            ESP_LOGE(TAG, "Error allocating memory");
            return ESP_ERR_NO_MEM;
        }
        resp_scan_thread_result__init(resp_payload);

        if (req->payload_case != NETWORK_SCAN_PAYLOAD__PAYLOAD_CMD_SCAN_THREAD_RESULT || !req->cmd_scan_thread_result) {
            ESP_LOGE(TAG, "Invalid Thread scan result command");
            resp->resp_scan_thread_result = resp_payload;
            return ESP_ERR_INVALID_ARG;
        }

        if (req->cmd_scan_thread_result->start_index >= CONFIG_NETWORK_PROV_SCAN_MAX_ENTRIES) {
            ESP_LOGE(TAG, "Thread scan result count/start_index out of bounds");
            resp->resp_scan_thread_result = resp_payload;
            return ESP_ERR_INVALID_ARG;
        }

        uint16_t req_thread_count = req->cmd_scan_thread_result->count;
        uint16_t max_thread_count = CONFIG_NETWORK_PROV_SCAN_MAX_ENTRIES - req->cmd_scan_thread_result->start_index;
        uint16_t effective_thread_count = MIN(req_thread_count, max_thread_count);
        if (req_thread_count != effective_thread_count) {
            ESP_LOGW(TAG, "Clamping Thread scan result count from %u to %u (start_index=%u)",
                     req_thread_count, effective_thread_count, req->cmd_scan_thread_result->start_index);
        }

        resp->status = STATUS__Success;
        resp->payload_case = NETWORK_SCAN_PAYLOAD__PAYLOAD_RESP_SCAN_THREAD_RESULT;
        resp->resp_scan_thread_result = resp_payload;
#ifdef CONFIG_NETWORK_PROV_NETWORK_TYPE_THREAD
        network_prov_scan_thread_result_t scan_result;
        memset(&scan_result, 0, sizeof(scan_result));
        ThreadScanResult **results = NULL;

        /* Allocate memory only if there are non-zero scan results */
        if (effective_thread_count) {
            results = (ThreadScanResult **) calloc(effective_thread_count,
                                                   sizeof(ThreadScanResult *));
            if (!results) {
                ESP_LOGE(TAG, "Failed to allocate memory for results array");
                return ESP_ERR_NO_MEM;
            }
        }
        resp_payload->entries = results;
        resp_payload->n_entries = effective_thread_count;

        /* If req->cmd_scan_result->count is 0, the below loop will
        * be skipped.
        */
        for (uint32_t i = 0; i < effective_thread_count; i++) {
            if (!h->thread_scan_result) {
                resp_payload->n_entries = i;
                resp->status = STATUS__InternalError;
                break;
            }
            /* start_index and count are validated above to sum to at most
             * CONFIG_NETWORK_PROV_SCAN_MAX_ENTRIES (max 255), so this cast is safe. */
            uint16_t result_index = (uint16_t)(i + req->cmd_scan_thread_result->start_index);
            err = h->thread_scan_result(result_index, &scan_result, &h->ctx);
            if (err != ESP_OK) {
                resp_payload->n_entries = i;
                resp->status = STATUS__InternalError;
                break;
            }

            results[i] = (ThreadScanResult *) malloc(sizeof(ThreadScanResult));
            if (!results[i]) {
                ESP_LOGE(TAG, "Failed to allocate memory for result entry");
                resp_payload->n_entries = i;
                resp->status = STATUS__InternalError;
                return ESP_ERR_NO_MEM;
            }
            thread_scan_result__init(results[i]);
            results[i]->pan_id = scan_result.pan_id;
            results[i]->channel = scan_result.channel;
            results[i]->rssi = scan_result.rssi;
            results[i]->lqi = scan_result.lqi;

            results[i]->ext_addr.len = sizeof(scan_result.ext_addr);
            results[i]->ext_addr.data = (uint8_t *)malloc(results[i]->ext_addr.len);
            if (!results[i]->ext_addr.data) {
                ESP_LOGE(TAG, "Failed to allocate memory for scan result entry extended address");
                results[i]->ext_addr.len = 0;
                resp_payload->n_entries = i + 1;
                resp->status = STATUS__InternalError;
                return ESP_ERR_NO_MEM;
            }
            memcpy(results[i]->ext_addr.data, scan_result.ext_addr, results[i]->ext_addr.len);

            results[i]->ext_pan_id.len = sizeof(scan_result.ext_pan_id);
            results[i]->ext_pan_id.data = (uint8_t *)malloc(results[i]->ext_pan_id.len);
            if (!results[i]->ext_pan_id.data) {
                ESP_LOGE(TAG, "Failed to allocate memory for scan result entry extended PAN ID");
                results[i]->ext_pan_id.len = 0;
                resp_payload->n_entries = i + 1;
                resp->status = STATUS__InternalError;
                return ESP_ERR_NO_MEM;
            }
            memcpy(results[i]->ext_pan_id.data, scan_result.ext_pan_id, results[i]->ext_pan_id.len);

            results[i]->network_name = (char *)malloc(sizeof(scan_result.network_name));
            if (!results[i]->network_name) {
                ESP_LOGE(TAG, "Failed to allocate memory for scan result entry network name");
                resp_payload->n_entries = i + 1;
                resp->status = STATUS__InternalError;
                return ESP_ERR_NO_MEM;
            }
            memcpy(results[i]->network_name, scan_result.network_name, sizeof(scan_result.network_name));
        }
#else // CONFIG_NETWORK_PROV_NETWORK_TYPE_THREAD
        resp->status = STATUS__InvalidArgument;
        err = ESP_ERR_INVALID_ARG;
#endif // !CONFIG_NETWORK_PROV_NETWORK_TYPE_THREAD
    }
    return err;
}


static int lookup_cmd_handler(int cmd_id)
{
    for (size_t i = 0; i < sizeof(cmd_table) / sizeof(network_prov_scan_cmd_t); i++) {
        if (cmd_table[i].cmd_num == cmd_id) {
            return i;
        }
    }

    return -1;
}

static void network_prov_scan_cmd_cleanup(NetworkScanPayload *resp, void *priv_data)
{
    switch (resp->msg) {
    case NETWORK_SCAN_MSG_TYPE__TypeRespScanWifiStart: {
        free(resp->resp_scan_wifi_start);
    }
    break;
    case NETWORK_SCAN_MSG_TYPE__TypeRespScanThreadStart: {
        free(resp->resp_scan_thread_start);
    }
    break;
    case NETWORK_SCAN_MSG_TYPE__TypeRespScanWifiStatus: {
        free(resp->resp_scan_wifi_status);
    }
    break;
    case NETWORK_SCAN_MSG_TYPE__TypeRespScanThreadStatus: {
        free(resp->resp_scan_thread_status);
    }
    break;

    case NETWORK_SCAN_MSG_TYPE__TypeRespScanWifiResult: {
        if (!resp->resp_scan_wifi_result) {
            return;
        }
#ifdef CONFIG_NETWORK_PROV_NETWORK_TYPE_WIFI
        if (resp->resp_scan_wifi_result->entries) {
            for (uint32_t i = 0; i < resp->resp_scan_wifi_result->n_entries; i++) {
                if (!resp->resp_scan_wifi_result->entries[i]) {
                    continue;
                }
                free(resp->resp_scan_wifi_result->entries[i]->ssid.data);
                free(resp->resp_scan_wifi_result->entries[i]->bssid.data);
                free(resp->resp_scan_wifi_result->entries[i]);
            }
            free(resp->resp_scan_wifi_result->entries);
        }
#endif // CONFIG_NETWORK_PROV_NETWORK_TYPE_WIFI
        free(resp->resp_scan_wifi_result);
    }
    break;
    case NETWORK_SCAN_MSG_TYPE__TypeRespScanThreadResult: {
        if (!resp->resp_scan_thread_result) {
            return;
        }
#ifdef CONFIG_NETWORK_PROV_NETWORK_TYPE_THREAD
        if (resp->resp_scan_thread_result->entries) {
            for (size_t i = 0; i < resp->resp_scan_thread_result->n_entries; i++) {
                if (!resp->resp_scan_thread_result->entries[i]) {
                    continue;
                }
                free(resp->resp_scan_thread_result->entries[i]->ext_addr.data);
                free(resp->resp_scan_thread_result->entries[i]->ext_pan_id.data);
                if (resp->resp_scan_thread_result->entries[i]->network_name != protobuf_c_empty_string) {
                    free(resp->resp_scan_thread_result->entries[i]->network_name);
                }
                free(resp->resp_scan_thread_result->entries[i]);
            }
            free(resp->resp_scan_thread_result->entries);
        }
#endif // CONFIG_NETWORK_PROV_NETWORK_TYPE_THREAD
        free(resp->resp_scan_thread_result);
    }
    break;
    default:
        ESP_LOGE(TAG, "Unsupported response type in cleanup_handler");
        break;
    }
    return;
}

static esp_err_t network_prov_scan_cmd_dispatcher(NetworkScanPayload *req,
        NetworkScanPayload *resp, void *priv_data)
{
    esp_err_t ret;

    ESP_LOGD(TAG, "In network_prov_scan_cmd_dispatcher Cmd=%d", req->msg);

    int cmd_index = lookup_cmd_handler(req->msg);
    if (cmd_index < 0) {
        ESP_LOGE(TAG, "Invalid command handler lookup");
        return ESP_FAIL;
    }

    ret = cmd_table[cmd_index].command_handler(req, resp, priv_data);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error executing command handler");
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t network_prov_scan_pack_response(NetworkScanPayload *resp,
                                                 uint8_t **outbuf, ssize_t *outlen)
{
    if (!resp || !outbuf || !outlen) {
        return ESP_ERR_INVALID_ARG;
    }

    *outlen = network_scan_payload__get_packed_size(resp);
    if (*outlen <= 0) {
        return ESP_FAIL;
    }

    *outbuf = (uint8_t *) malloc(*outlen);
    if (!*outbuf) {
        return ESP_ERR_NO_MEM;
    }

    network_scan_payload__pack(resp, *outbuf);
    ESP_LOGD(TAG, "Response packet size : %d", *outlen);
    return ESP_OK;
}

esp_err_t network_prov_scan_handler(uint32_t session_id, const uint8_t *inbuf, ssize_t inlen,
                                    uint8_t **outbuf, ssize_t *outlen, void *priv_data)
{
    NetworkScanPayload *req;
    NetworkScanPayload resp;
    esp_err_t ret = ESP_OK;

    req = network_scan_payload__unpack(NULL, inlen, inbuf);
    if (!req) {
        NetworkScanPayload decode_err_resp;
        network_scan_payload__init(&decode_err_resp);

        /* Ensure encrypted transport always has a non-empty protobuf payload. */
        decode_err_resp.msg = NETWORK_SCAN_MSG_TYPE__TypeRespScanWifiStatus;
        decode_err_resp.status = STATUS__InvalidArgument;

        ESP_LOGW(TAG, "Unable to unpack scan message (len=%d), returning error response", (int)inlen);
        return network_prov_scan_pack_response(&decode_err_resp, outbuf, outlen);
    }

    network_scan_payload__init(&resp);
    /* Validate req->msg before arithmetic to avoid signed overflow on attacker-controlled
     * wire values. For unknown commands the dispatcher returns ESP_FAIL without calling
     * any handler, so nothing is allocated and resp.msg = 0 is safe for cleanup. */
    if (lookup_cmd_handler(req->msg) >= 0) {
        resp.msg = req->msg + 1;
    }
    ret = network_prov_scan_cmd_dispatcher(req, &resp, priv_data);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Command dispatcher error %d", ret);
        /* Keep session stable: serialize an explicit error response. */
        resp.status = STATUS__InternalError;
        if (resp.msg == 0) {
            resp.msg = NETWORK_SCAN_MSG_TYPE__TypeRespScanWifiStatus;
        }
        ret = network_prov_scan_pack_response(&resp, outbuf, outlen);
        goto exit;
    }

    ret = network_prov_scan_pack_response(&resp, outbuf, outlen);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Invalid encoding for response");
        goto exit;
    }
exit:

    network_scan_payload__free_unpacked(req, NULL);
    network_prov_scan_cmd_cleanup(&resp, priv_data);
    return ret;
}
