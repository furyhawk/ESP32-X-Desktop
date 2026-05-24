#include <stdio.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <esp_log.h>
#include <esp_timer.h>
#include "lvgl_bsp.h"
#include "esp_lv_adapter.h"
#include "user_config.h"

static const char *TAG = "LvglPort";

static lv_display_t *disp = NULL;

esp_err_t Lvgl_lock(int timeout_ms)
{
  	return esp_lv_adapter_lock(timeout_ms);      
}

void Lvgl_unlock(void)
{
  	esp_lv_adapter_unlock();
}

#if LVGL_VERSION_MAJOR >= 9
static void rounder_event_cb(lv_event_t *e)
{
    lv_area_t *area = (lv_area_t *)lv_event_get_param(e);
    uint16_t x1 = area->x1;
    uint16_t x2 = area->x2;

    uint16_t y1 = area->y1;
    uint16_t y2 = area->y2;

    // round the start of coordinate down to the nearest 2M number
    area->x1 = (x1 >> 1) << 1;
    area->y1 = (y1 >> 1) << 1;
    // round the end of coordinate up to the nearest 2N+1 number
    area->x2 = ((x2 >> 1) << 1) + 1;
    area->y2 = ((y2 >> 1) << 1) + 1;
}
#else
static void bsp_lvgl_rounder_cb(lv_disp_drv_t *disp_drv, lv_area_t *area)
{
    uint16_t x1 = area->x1;
    uint16_t x2 = area->x2;

    uint16_t y1 = area->y1;
    uint16_t y2 = area->y2;

    // round the start of coordinate down to the nearest 2M number
    area->x1 = (x1 >> 1) << 1;
    area->y1 = (y1 >> 1) << 1;
    // round the end of coordinate up to the nearest 2N+1 number
    area->x2 = ((x2 >> 1) << 1) + 1;
    area->y2 = ((y2 >> 1) << 1) + 1;
}
#endif

esp_err_t Lvgl_PortInit(DisplayPort &display) {
	esp_lv_adapter_config_t cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG();
	cfg.task_stack_size = 8 * 1024;
    cfg.task_priority = 10;
    cfg.task_core_id = 0;
    cfg.stack_in_psram = false;

    esp_err_t ret = esp_lv_adapter_init(&cfg);
    if(ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_lv_adapter_init failed: %s", esp_err_to_name(ret));
        return ret;
    }
	esp_lcd_panel_handle_t panel_handle = display.Get_PanelHandle();
	esp_lcd_panel_io_handle_t io_handle = display.Get_IoHandle();
	esp_lv_adapter_display_config_t disp_cfg = ESP_LV_ADAPTER_DISPLAY_SPI_WITHOUT_PSRAM_DEFAULT_CONFIG(
        panel_handle,           		// LCD 面板句柄
        io_handle,        				// LCD 面板 IO 句柄（某些接口可为 NULL）
        BSP_LCD_H_RES,             		// 水平分辨率
        BSP_LCD_V_RES,             		// 垂直分辨率
        ESP_LV_ADAPTER_ROTATE_0 		// 旋转角度
    );

    static const uint16_t kBufferHeights[] = {100, 80, 60, 40, 30, 20, 10};
    disp = NULL;
    for(size_t i = 0; i < sizeof(kBufferHeights) / sizeof(kBufferHeights[0]); ++i) {
        disp_cfg.profile.buffer_height = kBufferHeights[i];
        disp = esp_lv_adapter_register_display(&disp_cfg);
        if(disp != NULL) {
            ESP_LOGI(TAG, "LVGL display registered with buffer_height=%u", (unsigned)kBufferHeights[i]);
            break;
        }
        ESP_LOGW(TAG, "LVGL display register failed with buffer_height=%u, retrying smaller buffer",
                 (unsigned)kBufferHeights[i]);
    }

    if(disp == NULL) {
        ESP_LOGE(TAG, "LVGL display registration failed for all buffer sizes");
        return ESP_ERR_NO_MEM;
    }
#if LVGL_VERSION_MAJOR >= 9
    lv_display_add_event_cb(disp, rounder_event_cb, LV_EVENT_INVALIDATE_AREA, NULL);
#else
    lv_disp_t *disp_v8 = (lv_disp_t *)disp;
    if (disp_v8 && disp_v8->driver) {
        disp_v8->driver->rounder_cb = bsp_lvgl_rounder_cb;
    }
#endif

    if(display.Get_TouchInitialized()) {
        esp_lcd_touch_handle_t touch_handle = display.Get_TouchHandle();
        esp_lv_adapter_touch_config_t touch_cfg = ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(disp, touch_handle);
        lv_indev_t *touch = esp_lv_adapter_register_touch(&touch_cfg);
        if(touch == NULL) {
            ESP_LOGE(TAG, "LVGL touch registration failed");
            return ESP_FAIL;
        }
    }

    ret = esp_lv_adapter_start();
    if(ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_lv_adapter_start failed: %s", esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}

void Lvgl_Refresh(void) {
    esp_lv_adapter_refresh_now(disp);
}

void Lvgl_HandleRotationChange(void)
{
    if(disp == NULL) {
        return;
    }

#if LVGL_VERSION_MAJOR >= 9
    lv_obj_t *screen = lv_screen_active();
#else
    lv_obj_t *screen = lv_scr_act();
#endif
    if(screen != NULL) {
        // Force a full redraw after MADCTL rotation since only dirty regions are usually refreshed.
        lv_obj_invalidate(screen);
    }
    esp_lv_adapter_refresh_now(disp);
}