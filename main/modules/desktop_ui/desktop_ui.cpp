#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>

#include <esp_log.h>
#include <esp_timer.h>
#include <esp_http_client.h>
#include <esp_crt_bundle.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

#include "lvgl.h"
#include "pcf85063a.h"
#include "cJSON.h"

#include "power_bsp.h"
#include "desktop_ui.h"
#include "libs/qrcode/lv_qrcode.h"
#include "wifi_provisioning.h"

#define TAG "desktop_ui"

typedef struct {
    lv_obj_t *time_label;
    lv_obj_t *date_label;
    lv_obj_t *battery_label;
    lv_obj_t *status_label;
    lv_obj_t *temperature_label;
    lv_obj_t *temperature_chart;
    lv_chart_series_t *temperature_series;
} desktop_widgets_t;

static desktop_widgets_t desktop_widgets = {};

static DisplayPort *desktop_display = NULL;
static pcf85063a_dev_t rtc_dev;
static bool rtc_ready = false;
static uint8_t desktop_brightness = 100;
static const char *SINGAPORE_TZ = "SGT-8";

static lv_obj_t *settings_panel = NULL;
static lv_obj_t *brightness_value_label = NULL;
static lv_obj_t *desktop_screen = NULL;
static lv_obj_t *prov_screen   = NULL;

static constexpr uint32_t TEMP_FETCH_INTERVAL_MS = 30000;
static constexpr uint32_t TEMP_HTTP_TIMEOUT_MS = 8000;
static constexpr size_t TEMP_HISTORY_POINTS = 24;
static const char *TEMP_SEARCH_URL = "http://wttr.in/?format=%t";
extern const uint8_t temp_api_ca_chain_pem_start[] asm("_binary_temp_api_ca_chain_pem_start");

typedef struct {
    char *buffer;
    size_t size;
    size_t capacity;
} http_buffer_t;

static SemaphoreHandle_t temp_data_mutex = NULL;
static bool temp_data_valid = false;
static float temp_latest_c = 0.0f;
static float temp_history_c[TEMP_HISTORY_POINTS] = {};
static size_t temp_history_count = 0;
static bool temp_task_started = false;

static float desktop_parse_temperature_value(const cJSON *temp_item, bool *ok)
{
    *ok = false;
    if(temp_item == NULL) {
        return 0.0f;
    }

    if(cJSON_IsNumber(temp_item)) {
        *ok = true;
        return (float)temp_item->valuedouble;
    }

    if(cJSON_IsString(temp_item) && temp_item->valuestring != NULL) {
        char *end_ptr = NULL;
        double parsed = strtod(temp_item->valuestring, &end_ptr);
        if(end_ptr != temp_item->valuestring) {
            *ok = true;
            return (float)parsed;
        }
    }

    return 0.0f;
}

static esp_err_t desktop_http_event_cb(esp_http_client_event_t *evt)
{
    if(evt == NULL || evt->user_data == NULL) {
        return ESP_OK;
    }

    http_buffer_t *response = (http_buffer_t *)evt->user_data;
    if(evt->event_id != HTTP_EVENT_ON_DATA || evt->data == NULL || evt->data_len <= 0) {
        return ESP_OK;
    }

    size_t needed = response->size + (size_t)evt->data_len + 1;
    if(needed > response->capacity) {
        size_t new_capacity = (response->capacity == 0) ? 1024 : response->capacity;
        while(new_capacity < needed) {
            new_capacity *= 2;
        }

        char *new_buffer = (char *)realloc(response->buffer, new_capacity);
        if(new_buffer == NULL) {
            return ESP_FAIL;
        }

        response->buffer = new_buffer;
        response->capacity = new_capacity;
    }

    memcpy(response->buffer + response->size, evt->data, (size_t)evt->data_len);
    response->size += (size_t)evt->data_len;
    response->buffer[response->size] = '\0';
    return ESP_OK;
}

static void desktop_store_temperature_history(const float *history, size_t history_count)
{
    if(temp_data_mutex == NULL) {
        return;
    }

    if(xSemaphoreTake(temp_data_mutex, pdMS_TO_TICKS(200)) != pdTRUE) {
        return;
    }

    size_t count = history_count;
    if(count > TEMP_HISTORY_POINTS) {
        count = TEMP_HISTORY_POINTS;
    }

    memset(temp_history_c, 0, sizeof(temp_history_c));
    if(count > 0) {
        memcpy(temp_history_c, history, count * sizeof(float));
        temp_latest_c = history[count - 1];
        temp_history_count = count;
        temp_data_valid = true;
    } else {
        temp_latest_c = 0.0f;
        temp_history_count = 0;
        temp_data_valid = false;
    }

    xSemaphoreGive(temp_data_mutex);
}

static bool desktop_parse_numeric_text(const char *text, float *value)
{
    if(text == NULL || value == NULL) {
        return false;
    }

    const char *start = text;
    while(*start != '\0') {
        if((*start >= '0' && *start <= '9') || *start == '-' || *start == '+') {
            break;
        }
        ++start;
    }

    if(*start == '\0') {
        return false;
    }

    char *end_ptr = NULL;
    double parsed = strtod(start, &end_ptr);
    if(end_ptr == start) {
        return false;
    }

    *value = (float)parsed;
    return true;
}

static void desktop_append_temperature_sample(float sample_c)
{
    if(temp_data_mutex == NULL) {
        return;
    }

    if(xSemaphoreTake(temp_data_mutex, pdMS_TO_TICKS(200)) != pdTRUE) {
        return;
    }

    if(temp_history_count >= TEMP_HISTORY_POINTS) {
        memmove(temp_history_c,
                temp_history_c + 1,
                (TEMP_HISTORY_POINTS - 1) * sizeof(float));
        temp_history_count = TEMP_HISTORY_POINTS - 1;
    }

    temp_history_c[temp_history_count++] = sample_c;
    temp_latest_c = sample_c;
    temp_data_valid = true;
    xSemaphoreGive(temp_data_mutex);
}

static esp_err_t desktop_fetch_temperature_history(void)
{
    http_buffer_t response = {};
    esp_err_t ret = ESP_FAIL;
    bool is_https = (strncmp(TEMP_SEARCH_URL, "https://", 8) == 0);
    int max_attempts = is_https ? 2 : 3;

    for(int attempt = 0; attempt < max_attempts; ++attempt) {
        bool use_bundle_fallback = (attempt == 1);
        response = {};

        esp_http_client_config_t config = {};
        config.url = TEMP_SEARCH_URL;
        config.method = HTTP_METHOD_GET;
        config.transport_type = is_https ? HTTP_TRANSPORT_OVER_SSL : HTTP_TRANSPORT_OVER_TCP;
        config.timeout_ms = TEMP_HTTP_TIMEOUT_MS;
        config.event_handler = desktop_http_event_cb;
        config.user_data = &response;
        if(is_https) {
            config.common_name = "api.furyhawk.lol";
            config.tls_version = ESP_HTTP_CLIENT_TLS_VER_TLS_1_2;
            if(use_bundle_fallback) {
                config.crt_bundle_attach = esp_crt_bundle_attach;
            } else {
                config.cert_pem = (const char *)temp_api_ca_chain_pem_start;
            }
        }

        esp_http_client_handle_t client = esp_http_client_init(&config);
        if(client == NULL) {
            ESP_LOGW(TAG, "Temperature HTTP client init failed");
            return ESP_FAIL;
        }

        ret = esp_http_client_perform(client);
        if(ret != ESP_OK) {
            ESP_LOGW(TAG,
                     "Temperature request failed (%s): %s (%d)",
                     is_https ? (use_bundle_fallback ? "bundle" : "embedded-ca") : "http",
                     esp_err_to_name(ret),
                     ret);
            esp_http_client_cleanup(client);
            free(response.buffer);
            response.buffer = NULL;
            if(is_https && !use_bundle_fallback) {
                continue;
            }
            return ret;
        }

        int status = esp_http_client_get_status_code(client);
        esp_http_client_cleanup(client);
        if(status != 200) {
            ESP_LOGW(TAG, "Temperature request returned HTTP %d", status);
            free(response.buffer);
            return ESP_FAIL;
        }

        break;
    }

    float plain_text_temp_c = 0.0f;
    if(desktop_parse_numeric_text(response.buffer, &plain_text_temp_c)) {
        desktop_append_temperature_sample(plain_text_temp_c);
        free(response.buffer);
        return ESP_OK;
    }

    cJSON *root = cJSON_Parse(response.buffer);
    free(response.buffer);
    if(root == NULL) {
        cJSON_Delete(root);
        ESP_LOGW(TAG, "Temperature response parsing failed");
        return ESP_FAIL;
    }

    float local_history[TEMP_HISTORY_POINTS] = {};
    size_t local_count = 0;

    if(cJSON_IsArray(root)) {
        int total = cJSON_GetArraySize(root);
        int start = total - (int)TEMP_HISTORY_POINTS;
        if(start < 0) {
            start = 0;
        }

        for(int i = start; i < total && local_count < TEMP_HISTORY_POINTS; ++i) {
            cJSON *entry = cJSON_GetArrayItem(root, i);
            cJSON *temp_item = cJSON_GetObjectItemCaseSensitive(entry, "temperature");
            bool ok = false;
            float value_c = desktop_parse_temperature_value(temp_item, &ok);
            if(ok) {
                local_history[local_count++] = value_c;
            }
        }
    } else if(cJSON_IsObject(root)) {
        cJSON *weather = cJSON_GetObjectItemCaseSensitive(root, "weather");
        if(cJSON_IsArray(weather)) {
            int day_count = cJSON_GetArraySize(weather);
            for(int d = 0; d < day_count; ++d) {
                cJSON *day_item = cJSON_GetArrayItem(weather, d);
                cJSON *hourly = cJSON_GetObjectItemCaseSensitive(day_item, "hourly");
                if(!cJSON_IsArray(hourly)) {
                    continue;
                }

                int hourly_count = cJSON_GetArraySize(hourly);
                for(int h = 0; h < hourly_count && local_count < TEMP_HISTORY_POINTS; ++h) {
                    cJSON *hourly_item = cJSON_GetArrayItem(hourly, h);
                    cJSON *temp_item = cJSON_GetObjectItemCaseSensitive(hourly_item, "tempC");
                    bool ok = false;
                    float value_c = desktop_parse_temperature_value(temp_item, &ok);
                    if(ok) {
                        local_history[local_count++] = value_c;
                    }
                }

                if(local_count >= TEMP_HISTORY_POINTS) {
                    break;
                }
            }
        }

        if(local_count == 0) {
            cJSON *current = cJSON_GetObjectItemCaseSensitive(root, "current_condition");
            if(cJSON_IsArray(current) && cJSON_GetArraySize(current) > 0) {
                cJSON *first = cJSON_GetArrayItem(current, 0);
                cJSON *temp_item = cJSON_GetObjectItemCaseSensitive(first, "temp_C");
                bool ok = false;
                float value_c = desktop_parse_temperature_value(temp_item, &ok);
                if(ok) {
                    local_history[local_count++] = value_c;
                }
            }
        }
    }

    if(local_count > TEMP_HISTORY_POINTS) {
        size_t keep = TEMP_HISTORY_POINTS;
        memmove(local_history, local_history + (local_count - keep), keep * sizeof(float));
        local_count = keep;
    }

    if(local_count == 0) {
        cJSON_Delete(root);
        ESP_LOGW(TAG, "Temperature response parsing failed");
        return ESP_FAIL;
    }

    cJSON_Delete(root);
    desktop_store_temperature_history(local_history, local_count);
    return (local_count > 0) ? ESP_OK : ESP_FAIL;
}

static void desktop_temperature_task(void *arg)
{
    LV_UNUSED(arg);

    while(1) {
        if(!WifiProvisioning_IsProvisioning() &&
           WifiProvisioning_IsConnected() &&
           WifiProvisioning_IsSystemTimeSynchronized()) {
            (void)desktop_fetch_temperature_history();
        }
        vTaskDelay(pdMS_TO_TICKS(TEMP_FETCH_INTERVAL_MS));
    }
}

static void desktop_update_temperature_widgets(void)
{
    float local_history[TEMP_HISTORY_POINTS] = {};
    size_t local_count = 0;
    float local_latest = 0.0f;
    bool local_valid = false;

    if(temp_data_mutex != NULL && xSemaphoreTake(temp_data_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        local_count = temp_history_count;
        if(local_count > TEMP_HISTORY_POINTS) {
            local_count = TEMP_HISTORY_POINTS;
        }

        if(local_count > 0) {
            memcpy(local_history, temp_history_c, local_count * sizeof(float));
        }

        local_latest = temp_latest_c;
        local_valid = temp_data_valid;
        xSemaphoreGive(temp_data_mutex);
    }

    if(desktop_widgets.temperature_label != NULL) {
        char temp_text[40] = {0};
        if(local_valid && local_count > 0) {
            snprintf(temp_text, sizeof(temp_text), "Temperature: %.1f C", local_latest);
        } else {
            snprintf(temp_text, sizeof(temp_text), "Temperature: --.- C");
        }
        lv_label_set_text(desktop_widgets.temperature_label, temp_text);
    }

    if(desktop_widgets.temperature_chart == NULL || desktop_widgets.temperature_series == NULL) {
        return;
    }

    lv_chart_set_point_count(desktop_widgets.temperature_chart, TEMP_HISTORY_POINTS);
    for(uint32_t i = 0; i < TEMP_HISTORY_POINTS; ++i) {
        lv_chart_set_value_by_id(desktop_widgets.temperature_chart,
                                 desktop_widgets.temperature_series,
                                 i,
                                 LV_CHART_POINT_NONE);
    }

    if(!local_valid || local_count == 0) {
        lv_chart_set_range(desktop_widgets.temperature_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 500);
        lv_chart_refresh(desktop_widgets.temperature_chart);
        return;
    }

    float min_temp = local_history[0];
    float max_temp = local_history[0];
    for(size_t i = 1; i < local_count; ++i) {
        if(local_history[i] < min_temp) {
            min_temp = local_history[i];
        }
        if(local_history[i] > max_temp) {
            max_temp = local_history[i];
        }
    }

    int32_t range_min = (int32_t)(min_temp * 10.0f) - 5;
    int32_t range_max = (int32_t)(max_temp * 10.0f) + 5;
    if(range_max - range_min < 30) {
        int32_t mid = (range_min + range_max) / 2;
        range_min = mid - 15;
        range_max = mid + 15;
    }

    lv_chart_set_range(desktop_widgets.temperature_chart,
                       LV_CHART_AXIS_PRIMARY_Y,
                       (lv_coord_t)range_min,
                       (lv_coord_t)range_max);

    size_t offset = TEMP_HISTORY_POINTS - local_count;
    for(size_t i = 0; i < local_count; ++i) {
        int32_t scaled = (int32_t)(local_history[i] * 10.0f);
        lv_chart_set_value_by_id(desktop_widgets.temperature_chart,
                                 desktop_widgets.temperature_series,
                                 (uint32_t)(offset + i),
                                 (lv_coord_t)scaled);
    }

    lv_chart_refresh(desktop_widgets.temperature_chart);
}

static const char *desktop_battery_symbol(int battery_percent, bool is_charging)
{
    if(is_charging) {
        return LV_SYMBOL_CHARGE;
    }

    if(battery_percent >= 80) {
        return LV_SYMBOL_BATTERY_FULL;
    }
    if(battery_percent >= 55) {
        return LV_SYMBOL_BATTERY_3;
    }
    if(battery_percent >= 30) {
        return LV_SYMBOL_BATTERY_2;
    }
    if(battery_percent >= 10) {
        return LV_SYMBOL_BATTERY_1;
    }
    return LV_SYMBOL_BATTERY_EMPTY;
}

static void desktop_read_time(char *time_text, size_t time_text_size, char *date_text, size_t date_text_size)
{
    if(rtc_ready) {
        pcf85063a_datetime_t date_time = {};
        if(pcf85063a_get_time_date(&rtc_dev, &date_time) == ESP_OK) {
            snprintf(time_text, time_text_size, "%02d:%02d:%02d", date_time.hour, date_time.min, date_time.sec);
            snprintf(date_text, date_text_size, "%04d-%02d-%02d", date_time.year, date_time.month, date_time.day);
            return;
        }

        rtc_ready = false;
        ESP_LOGW(TAG, "RTC read failed, falling back to uptime clock");
    }

    uint64_t total_seconds = esp_timer_get_time() / 1000000ULL;
    uint32_t seconds = total_seconds % 60ULL;
    uint32_t minutes = (total_seconds / 60ULL) % 60ULL;
    uint32_t hours = (total_seconds / 3600ULL) % 24ULL;

    snprintf(time_text, time_text_size, "%02lu:%02lu:%02lu",
             (unsigned long)hours,
             (unsigned long)minutes,
             (unsigned long)seconds);
    snprintf(date_text, date_text_size, "RTC unavailable");
}

static void desktop_set_singapore_timezone(void)
{
    setenv("TZ", SINGAPORE_TZ, 1);
    tzset();
}

esp_err_t DesktopUI_SyncSystemTimeFromRtc(void)
{
    if(!rtc_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    pcf85063a_datetime_t date_time = {};
    esp_err_t ret = pcf85063a_get_time_date(&rtc_dev, &date_time);
    if(ret != ESP_OK) {
        rtc_ready = false;
        ESP_LOGW(TAG, "Failed to read RTC while seeding system time (error: %d)", ret);
        return ret;
    }

    desktop_set_singapore_timezone();

    struct tm local_time = {};
    local_time.tm_year = (int)date_time.year - 1900;
    local_time.tm_mon = (int)date_time.month - 1;
    local_time.tm_mday = (int)date_time.day;
    local_time.tm_hour = (int)date_time.hour;
    local_time.tm_min = (int)date_time.min;
    local_time.tm_sec = (int)date_time.sec;
    local_time.tm_wday = (int)date_time.dotw;
    local_time.tm_isdst = 0;

    time_t epoch = mktime(&local_time);
    if(epoch == (time_t)-1) {
        ESP_LOGW(TAG, "RTC contained an invalid local time, skipping system clock seed");
        return ESP_ERR_INVALID_RESPONSE;
    }

    struct timeval now = {
        .tv_sec = epoch,
        .tv_usec = 0,
    };

    if(settimeofday(&now, NULL) != 0) {
        ESP_LOGE(TAG, "Failed to seed system time from RTC");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Seeded system clock from RTC: %04u-%02u-%02u %02u:%02u:%02u",
             date_time.year,
             date_time.month,
             date_time.day,
             date_time.hour,
             date_time.min,
             date_time.sec);
    return ESP_OK;
}

esp_err_t DesktopUI_SyncRtcFromSystemTime(void)
{
    if(!rtc_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    desktop_set_singapore_timezone();

    time_t now = time(NULL);
    if(now <= 0) {
        return ESP_ERR_INVALID_STATE;
    }

    struct tm local_time = {};
    if(localtime_r(&now, &local_time) == NULL) {
        return ESP_FAIL;
    }

    pcf85063a_datetime_t date_time = {
        .year = (uint16_t)(local_time.tm_year + 1900),
        .month = (uint8_t)(local_time.tm_mon + 1),
        .day = (uint8_t)local_time.tm_mday,
        .dotw = (uint8_t)local_time.tm_wday,
        .hour = (uint8_t)local_time.tm_hour,
        .min = (uint8_t)local_time.tm_min,
        .sec = (uint8_t)local_time.tm_sec,
    };

    esp_err_t ret = pcf85063a_set_time_date(&rtc_dev, date_time);
    if(ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to update RTC from system time (error: %d)", ret);
        return ret;
    }

    ESP_LOGI(TAG, "RTC updated from synced local time: %04u-%02u-%02u %02u:%02u:%02u",
             date_time.year,
             date_time.month,
             date_time.day,
             date_time.hour,
             date_time.min,
             date_time.sec);
    return ESP_OK;
}

static void desktop_refresh(lv_timer_t *timer)
{
    LV_UNUSED(timer);

    /* Auto-dismiss provisioning screen once provisioning completes */
    if(prov_screen != NULL && !WifiProvisioning_IsProvisioning()) {
        lv_obj_delete(prov_screen);
        prov_screen = NULL;
        if(desktop_screen != NULL) {
            lv_screen_load(desktop_screen);
        }
    }

    char time_text[16] = {0};
    char date_text[24] = {0};
    char battery_text[48] = {0};
    char status_text[80] = {0};
    char wifi_text[32] = {0};
    char power_text[40] = {0};

    desktop_read_time(time_text, sizeof(time_text), date_text, sizeof(date_text));

    bool is_battery_connected = Axp2101_IsBatteryConnected();
    bool is_charging = Axp2101_IsCharging();
    int battery_percent = Axp2101_GetBatteryPercent();
    uint16_t battery_voltage_mv = Axp2101_GetBatteryVoltageMv();

    if(is_battery_connected && battery_percent >= 0) {
        snprintf(battery_text, sizeof(battery_text), "%s %d%%  %umV",
                 desktop_battery_symbol(battery_percent, is_charging),
                 battery_percent,
                 battery_voltage_mv);
        snprintf(power_text, sizeof(power_text), "%s", is_charging ? "Charging" : "Battery power");
    } else {
        snprintf(battery_text, sizeof(battery_text), "%s External power",
                 is_charging ? LV_SYMBOL_CHARGE : LV_SYMBOL_BATTERY_EMPTY);
        snprintf(power_text, sizeof(power_text), "%s", rtc_ready ? "Standby" : "Standby · uptime clock");
    }

    WifiProvisioning_GetConnectionStatus(wifi_text, sizeof(wifi_text));
    snprintf(status_text, sizeof(status_text), "%s | %s", wifi_text, power_text);

    lv_label_set_text(desktop_widgets.time_label, time_text);
    lv_label_set_text(desktop_widgets.date_label, date_text);
    lv_label_set_text(desktop_widgets.battery_label, battery_text);
    lv_label_set_text(desktop_widgets.status_label, status_text);
    desktop_update_temperature_widgets();
}

static void desktop_set_brightness(uint8_t brightness)
{
    desktop_brightness = brightness;

    if(desktop_display != NULL) {
        desktop_display->Set_Backlight(desktop_brightness);
    }

    if(brightness_value_label != NULL) {
        char value_text[8] = {0};
        snprintf(value_text, sizeof(value_text), "%u%%", desktop_brightness);
        lv_label_set_text(brightness_value_label, value_text);
    }
}

static void desktop_settings_close_event(lv_event_t *e)
{
    LV_UNUSED(e);
    if(settings_panel != NULL) {
        lv_obj_add_flag(settings_panel, LV_OBJ_FLAG_HIDDEN);
    }
}

static void desktop_settings_open_event(lv_event_t *e)
{
    LV_UNUSED(e);
    if(settings_panel != NULL) {
        lv_obj_clear_flag(settings_panel, LV_OBJ_FLAG_HIDDEN);
    }
}

static void desktop_brightness_slider_event(lv_event_t *e)
{
    lv_obj_t *slider = (lv_obj_t *)lv_event_get_target(e);
    int32_t slider_value = lv_slider_get_value(slider);
    if(slider_value < 0) {
        slider_value = 0;
    }
    if(slider_value > 100) {
        slider_value = 100;
    }
    desktop_set_brightness((uint8_t)slider_value);
}

static void desktop_wifi_setup_event(lv_event_t *e)
{
    LV_UNUSED(e);

    esp_err_t ret = WifiProvisioning_Reprovision();
    if(ret != ESP_OK) {
        ESP_LOGW(TAG, "Unable to start Wi-Fi reprovisioning (error: %d)", ret);
        return;
    }

    char svc_name[20] = {0};
    char qr_payload[200] = {0};
    WifiProvisioning_GetServiceName(svc_name, sizeof(svc_name));
    WifiProvisioning_GetQRPayload(qr_payload, sizeof(qr_payload));

    if(settings_panel != NULL) {
        lv_obj_add_flag(settings_panel, LV_OBJ_FLAG_HIDDEN);
    }

    DesktopUI_ShowProvisioningQR(svc_name, "prov1234", qr_payload);
}

static void desktop_init_rtc(I2cMasterBus *i2c_bus)
{
    esp_err_t ret = pcf85063a_init(&rtc_dev, i2c_bus->Get_I2cBusHandle(), PCF85063A_ADDRESS);
    if(ret == ESP_OK) {
        rtc_ready = true;
        ESP_LOGI(TAG, "RTC initialized");
        return;
    }

    rtc_ready = false;
    ESP_LOGW(TAG, "Failed to initialize PCF85063 (error: %d)", ret);
}

static void desktop_create_ui(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x0B1020), 0);
    lv_obj_set_style_bg_grad_color(screen, lv_color_hex(0x1A4D8F), 0);
    lv_obj_set_style_bg_grad_dir(screen, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 20, 0);

    lv_obj_t *top_bar = lv_obj_create(screen);
    lv_obj_set_size(top_bar, lv_pct(100), 72);
    lv_obj_align(top_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(top_bar, lv_color_hex(0x12233E), 0);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_80, 0);
    lv_obj_set_style_border_width(top_bar, 0, 0);
    lv_obj_set_style_radius(top_bar, 24, 0);
    lv_obj_set_style_pad_hor(top_bar, 20, 0);
    lv_obj_set_style_pad_ver(top_bar, 12, 0);
    lv_obj_set_layout(top_bar, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(top_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top_bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *title_label = lv_label_create(top_bar);
    lv_label_set_text(title_label, "ESP32-X Desktop");
    lv_obj_set_style_text_color(title_label, lv_color_hex(0xF4F7FB), 0);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_20, 0);

    desktop_widgets.battery_label = lv_label_create(top_bar);
    lv_label_set_text(desktop_widgets.battery_label, "Battery");
    lv_obj_set_style_text_color(desktop_widgets.battery_label, lv_color_hex(0xD6E4FF), 0);
    lv_obj_set_style_text_font(desktop_widgets.battery_label, &lv_font_montserrat_20, 0);

    lv_obj_t *hero_card = lv_obj_create(screen);
    lv_obj_set_size(hero_card, lv_pct(100), 220);
    lv_obj_align(hero_card, LV_ALIGN_CENTER, 0, -24);
    lv_obj_set_style_bg_color(hero_card, lv_color_hex(0x10284A), 0);
    lv_obj_set_style_bg_opa(hero_card, LV_OPA_80, 0);
    lv_obj_set_style_border_width(hero_card, 0, 0);
    lv_obj_set_style_radius(hero_card, 32, 0);
    lv_obj_set_style_pad_all(hero_card, 24, 0);
    lv_obj_set_layout(hero_card, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(hero_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(hero_card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    desktop_widgets.time_label = lv_label_create(hero_card);
    lv_label_set_text(desktop_widgets.time_label, "--:--:--");
    lv_obj_set_style_text_color(desktop_widgets.time_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(desktop_widgets.time_label, &lv_font_montserrat_26, 0);

    desktop_widgets.date_label = lv_label_create(hero_card);
    lv_label_set_text(desktop_widgets.date_label, "Waiting for RTC");
    lv_obj_set_style_text_color(desktop_widgets.date_label, lv_color_hex(0xB8CAE6), 0);
    lv_obj_set_style_text_font(desktop_widgets.date_label, &lv_font_montserrat_20, 0);

    desktop_widgets.temperature_label = lv_label_create(hero_card);
    lv_label_set_text(desktop_widgets.temperature_label, "Temperature: --.- C");
    lv_obj_set_style_text_color(desktop_widgets.temperature_label, lv_color_hex(0xD6E4FF), 0);
    lv_obj_set_style_text_font(desktop_widgets.temperature_label, &lv_font_montserrat_16, 0);

    lv_obj_t *chart_card = lv_obj_create(screen);
    lv_obj_set_size(chart_card, lv_pct(100), 125);
    lv_obj_align(chart_card, LV_ALIGN_CENTER, 0, 142);
    lv_obj_set_style_bg_color(chart_card, lv_color_hex(0x0E2240), 0);
    lv_obj_set_style_bg_opa(chart_card, LV_OPA_80, 0);
    lv_obj_set_style_border_width(chart_card, 0, 0);
    lv_obj_set_style_radius(chart_card, 24, 0);
    lv_obj_set_style_pad_all(chart_card, 12, 0);

    lv_obj_t *chart_title = lv_label_create(chart_card);
    lv_label_set_text(chart_title, "Temperature history");
    lv_obj_set_style_text_color(chart_title, lv_color_hex(0xD6E4FF), 0);
    lv_obj_set_style_text_font(chart_title, &lv_font_montserrat_14, 0);
    lv_obj_align(chart_title, LV_ALIGN_TOP_LEFT, 0, -4);

    desktop_widgets.temperature_chart = lv_chart_create(chart_card);
    lv_obj_set_size(desktop_widgets.temperature_chart, lv_pct(100), 90);
    lv_obj_align(desktop_widgets.temperature_chart, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(desktop_widgets.temperature_chart, LV_OPA_0, 0);
    lv_obj_set_style_border_width(desktop_widgets.temperature_chart, 0, 0);
    lv_chart_set_type(desktop_widgets.temperature_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_update_mode(desktop_widgets.temperature_chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_chart_set_div_line_count(desktop_widgets.temperature_chart, 3, 6);
    lv_chart_set_point_count(desktop_widgets.temperature_chart, TEMP_HISTORY_POINTS);
    lv_chart_set_range(desktop_widgets.temperature_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 500);
    desktop_widgets.temperature_series = lv_chart_add_series(desktop_widgets.temperature_chart,
                                                             lv_color_hex(0x55D3FF),
                                                             LV_CHART_AXIS_PRIMARY_Y);

    for(uint32_t i = 0; i < TEMP_HISTORY_POINTS; ++i) {
        lv_chart_set_value_by_id(desktop_widgets.temperature_chart,
                                 desktop_widgets.temperature_series,
                                 i,
                                 LV_CHART_POINT_NONE);
    }

    desktop_widgets.status_label = lv_label_create(screen);
    lv_label_set_text(desktop_widgets.status_label, "Initializing");
    lv_obj_align(desktop_widgets.status_label, LV_ALIGN_BOTTOM_LEFT, 8, -4);
    lv_obj_set_style_text_color(desktop_widgets.status_label, lv_color_hex(0xD6E4FF), 0);

    lv_obj_t *settings_button = lv_btn_create(screen);
    lv_obj_align(settings_button, LV_ALIGN_BOTTOM_RIGHT, -8, -8);
    lv_obj_set_size(settings_button, 140, 48);
    lv_obj_set_style_radius(settings_button, 20, 0);
    lv_obj_set_style_bg_color(settings_button, lv_color_hex(0x213A66), 0);
    lv_obj_add_event_cb(settings_button, desktop_settings_open_event, LV_EVENT_CLICKED, NULL);

    lv_obj_t *settings_button_label = lv_label_create(settings_button);
    lv_label_set_text(settings_button_label, LV_SYMBOL_SETTINGS " Settings");
    lv_obj_set_style_text_color(settings_button_label, lv_color_hex(0xF0F5FF), 0);
    lv_obj_center(settings_button_label);

    settings_panel = lv_obj_create(screen);
    lv_obj_set_size(settings_panel, 420, 300);
    lv_obj_align(settings_panel, LV_ALIGN_CENTER, 0, 26);
    lv_obj_set_style_bg_color(settings_panel, lv_color_hex(0x0E1D36), 0);
    lv_obj_set_style_bg_opa(settings_panel, LV_OPA_90, 0);
    lv_obj_set_style_radius(settings_panel, 26, 0);
    lv_obj_set_style_border_width(settings_panel, 0, 0);
    lv_obj_set_style_pad_all(settings_panel, 18, 0);
    lv_obj_add_flag(settings_panel, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *settings_title = lv_label_create(settings_panel);
    lv_label_set_text(settings_title, "Settings");
    lv_obj_set_style_text_color(settings_title, lv_color_hex(0xF7FAFF), 0);
    lv_obj_set_style_text_font(settings_title, &lv_font_montserrat_20, 0);
    lv_obj_align(settings_title, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *close_button = lv_btn_create(settings_panel);
    lv_obj_set_size(close_button, 40, 40);
    lv_obj_align(close_button, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_style_radius(close_button, 20, 0);
    lv_obj_set_style_bg_color(close_button, lv_color_hex(0x2C487A), 0);
    lv_obj_add_event_cb(close_button, desktop_settings_close_event, LV_EVENT_CLICKED, NULL);

    lv_obj_t *close_label = lv_label_create(close_button);
    lv_label_set_text(close_label, LV_SYMBOL_CLOSE);
    lv_obj_center(close_label);

    lv_obj_t *brightness_title = lv_label_create(settings_panel);
    lv_label_set_text(brightness_title, "Brightness");
    lv_obj_set_style_text_color(brightness_title, lv_color_hex(0xD8E4F8), 0);
    lv_obj_align(brightness_title, LV_ALIGN_TOP_LEFT, 0, 64);

    brightness_value_label = lv_label_create(settings_panel);
    lv_label_set_text(brightness_value_label, "100%");
    lv_obj_set_style_text_color(brightness_value_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(brightness_value_label, LV_ALIGN_TOP_RIGHT, 0, 64);

    lv_obj_t *brightness_slider = lv_slider_create(settings_panel);
    lv_obj_set_width(brightness_slider, lv_pct(100));
    lv_obj_align(brightness_slider, LV_ALIGN_TOP_MID, 0, 106);
    lv_slider_set_range(brightness_slider, 0, 100);
    lv_slider_set_value(brightness_slider, desktop_brightness, LV_ANIM_OFF);
    lv_obj_add_event_cb(brightness_slider, desktop_brightness_slider_event, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *wifi_title = lv_label_create(settings_panel);
    lv_label_set_text(wifi_title, "Wi-Fi");
    lv_obj_set_style_text_color(wifi_title, lv_color_hex(0xD8E4F8), 0);
    lv_obj_align(wifi_title, LV_ALIGN_TOP_LEFT, 0, 150);

    lv_obj_t *wifi_button = lv_btn_create(settings_panel);
    lv_obj_set_size(wifi_button, lv_pct(100), 48);
    lv_obj_align(wifi_button, LV_ALIGN_TOP_MID, 0, 182);
    lv_obj_set_style_radius(wifi_button, 16, 0);
    lv_obj_set_style_bg_color(wifi_button, lv_color_hex(0x2C487A), 0);
    lv_obj_add_event_cb(wifi_button, desktop_wifi_setup_event, LV_EVENT_CLICKED, NULL);

    lv_obj_t *wifi_button_label = lv_label_create(wifi_button);
    lv_label_set_text(wifi_button_label, LV_SYMBOL_WIFI " Re-provision Wi-Fi");
    lv_obj_set_style_text_color(wifi_button_label, lv_color_hex(0xF7FAFF), 0);
    lv_obj_center(wifi_button_label);

    lv_screen_load(screen);
    desktop_set_brightness(desktop_brightness);
    desktop_refresh(NULL);
    lv_timer_create(desktop_refresh, 1000, NULL);
    desktop_screen = screen;
}

void DesktopUI_Init(I2cMasterBus *i2c_bus, DisplayPort *display)
{
    desktop_display = display;
    if(temp_data_mutex == NULL) {
        temp_data_mutex = xSemaphoreCreateMutex();
    }
    desktop_init_rtc(i2c_bus);
    DesktopUI_SyncSystemTimeFromRtc();
    desktop_create_ui();
}

void DesktopUI_StartTelemetry(void)
{
    if(temp_task_started) {
        return;
    }

    if(xTaskCreate(desktop_temperature_task, "desktop_temp", 8192, NULL, 3, NULL) == pdPASS) {
        temp_task_started = true;
    } else {
        ESP_LOGW(TAG, "Failed to start desktop telemetry task");
    }
}

void DesktopUI_ShowProvisioningQR(const char *service_name, const char *service_key, const char *qr_payload)
{
    if(prov_screen != NULL) {
        lv_obj_delete(prov_screen);
        prov_screen = NULL;
    }

    prov_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(prov_screen, lv_color_hex(0x0B1020), 0);
    lv_obj_set_style_pad_all(prov_screen, 16, 0);

    /* Title */
    lv_obj_t *title = lv_label_create(prov_screen);
    lv_label_set_text(title, LV_SYMBOL_WIFI "  Wi-Fi Setup");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    /* QR code — enlarged for easier scanning on-device */
    lv_obj_t *qr = lv_qrcode_create(prov_screen);
    lv_qrcode_set_size(qr, 280);
    lv_qrcode_set_dark_color(qr, lv_color_black());
    lv_qrcode_set_light_color(qr, lv_color_white());
    lv_qrcode_update(qr, qr_payload, (uint32_t)strlen(qr_payload));
    lv_obj_align(qr, LV_ALIGN_CENTER, 0, -4);

    /* Instruction row: connect info */
    char connect_text[64] = {0};
    snprintf(connect_text, sizeof(connect_text), "SSID: %s  |  Pass: %s", service_name, service_key);
    lv_obj_t *connect_label = lv_label_create(prov_screen);
    lv_label_set_text(connect_label, connect_text);
    lv_obj_set_style_text_color(connect_label, lv_color_hex(0xB8CAE6), 0);
    lv_obj_set_style_text_font(connect_label, &lv_font_montserrat_12, 0);
    lv_obj_set_width(connect_label, 440);
    lv_label_set_long_mode(connect_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(connect_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(connect_label, LV_ALIGN_BOTTOM_MID, 0, -34);

    lv_obj_t *hint_label = lv_label_create(prov_screen);
    lv_label_set_text(hint_label, "Scan with Espressif Provisioning app");
    lv_obj_set_style_text_color(hint_label, lv_color_hex(0x7A9DC4), 0);
    lv_obj_set_style_text_font(hint_label, &lv_font_montserrat_12, 0);
    lv_obj_align(hint_label, LV_ALIGN_BOTTOM_MID, 0, -12);

    lv_screen_load(prov_screen);
}