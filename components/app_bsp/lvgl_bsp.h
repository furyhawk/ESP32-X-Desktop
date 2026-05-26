#pragma once

#include "lvgl.h"
#include "display_bsp.h"

esp_err_t Lvgl_PortInit(DisplayPort &display);
void Lvgl_Refresh(void);
void Lvgl_HandleRotationChange(void);
esp_err_t Lvgl_lock(int timeout_ms);
void Lvgl_unlock(void);