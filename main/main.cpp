
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <esp_log.h>
#include <esp_spiffs.h>
#include <esp_timer.h>

#include "lvgl_bsp.h"
#include "lvgl.h"
#include "pcf85063a.h"
#include "power_bsp.h"

#define TAG "main"

#define Brightness_Test_EN  1

I2cMasterBus user_i2cbus(7,8,0); //scl,sda,i2c_port
DisplayPort *user_display = NULL;

typedef struct {
    lv_obj_t *time_label;
    lv_obj_t *date_label;
    lv_obj_t *battery_label;
    lv_obj_t *status_label;
} desktop_widgets_t;

static desktop_widgets_t desktop_widgets = {};
static pcf85063a_dev_t rtc_dev;
static bool rtc_ready = false;

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

static void desktop_refresh(lv_timer_t *timer)
{
    LV_UNUSED(timer);

    char time_text[16] = {0};
    char date_text[24] = {0};
    char battery_text[48] = {0};
    char status_text[48] = {0};

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
        snprintf(status_text, sizeof(status_text), "%s", is_charging ? "Charging" : "Battery power");
    } else {
        snprintf(battery_text, sizeof(battery_text), "%s External power",
                 is_charging ? LV_SYMBOL_CHARGE : LV_SYMBOL_BATTERY_EMPTY);
        snprintf(status_text, sizeof(status_text), "%s", rtc_ready ? "Standby" : "Standby · uptime clock");
    }

    lv_label_set_text(desktop_widgets.time_label, time_text);
    lv_label_set_text(desktop_widgets.date_label, date_text);
    lv_label_set_text(desktop_widgets.battery_label, battery_text);
    lv_label_set_text(desktop_widgets.status_label, status_text);
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
    lv_label_set_text(title_label, "ESP32-C6 Desktop");
    lv_obj_set_style_text_color(title_label, lv_color_hex(0xF4F7FB), 0);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_20, 0);

    desktop_widgets.battery_label = lv_label_create(top_bar);
    lv_label_set_text(desktop_widgets.battery_label, "Battery");
    lv_obj_set_style_text_color(desktop_widgets.battery_label, lv_color_hex(0xD6E4FF), 0);
    lv_obj_set_style_text_font(desktop_widgets.battery_label, &lv_font_montserrat_20, 0);

    lv_obj_t *hero_card = lv_obj_create(screen);
    lv_obj_set_size(hero_card, lv_pct(100), 250);
    lv_obj_align(hero_card, LV_ALIGN_CENTER, 0, 10);
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
    lv_obj_set_style_text_font(desktop_widgets.time_label, &lv_font_montserrat_20, 0);

    desktop_widgets.date_label = lv_label_create(hero_card);
    lv_label_set_text(desktop_widgets.date_label, "Waiting for RTC");
    lv_obj_set_style_text_color(desktop_widgets.date_label, lv_color_hex(0xB8CAE6), 0);

    desktop_widgets.status_label = lv_label_create(screen);
    lv_label_set_text(desktop_widgets.status_label, "Initializing");
    lv_obj_align(desktop_widgets.status_label, LV_ALIGN_BOTTOM_LEFT, 8, -4);
    lv_obj_set_style_text_color(desktop_widgets.status_label, lv_color_hex(0xD6E4FF), 0);

    lv_screen_load(screen);
    desktop_refresh(NULL);
    lv_timer_create(desktop_refresh, 1000, NULL);
}

static void desktop_init_rtc(void)
{
    esp_err_t ret = pcf85063a_init(&rtc_dev, user_i2cbus.Get_I2cBusHandle(), PCF85063A_ADDRESS);
    if(ret == ESP_OK) {
        rtc_ready = true;
        ESP_LOGI(TAG, "RTC initialized");
        return;
    }

    rtc_ready = false;
    ESP_LOGW(TAG, "Failed to initialize PCF85063 (error: %d)", ret);
}

#if (Brightness_Test_EN == 1) 

void Brightness_Test(void *arg) {
    uint8_t back = 100;
    while(1) {
        if(Lvgl_lock(-1) == ESP_OK) {
		    user_display->Set_Backlight(back);
            Lvgl_unlock();
  	    }
        vTaskDelay(pdMS_TO_TICKS(2000));
        if(back > 0) {
            back = back - 20;
        } else {
            back = 100;
        }
    }
}

#endif

extern "C" void app_main(void)
{
    Custom_PmicPortInit(&user_i2cbus,0x34);
    desktop_init_rtc();
    user_display = new DisplayPort(user_i2cbus,480,480);
    user_display->DisplayPort_TouchInit();
    Lvgl_PortInit(*user_display);
    if(Lvgl_lock(-1) == ESP_OK) {
		desktop_create_ui();
    	Lvgl_unlock();
  	}
#if (Brightness_Test_EN == 1) 
    xTaskCreatePinnedToCore(Brightness_Test, "Brightness_Test", 3 * 1024, NULL, 3, NULL,0);
#endif
}
