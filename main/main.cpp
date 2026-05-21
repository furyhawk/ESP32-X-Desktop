
#include <esp_log.h>

#include "lvgl_bsp.h"
#include "power_bsp.h"
#include "desktop_ui.h"
#include "wifi_provisioning.h"

#define TAG "main"

I2cMasterBus user_i2cbus(7,8,0); //scl,sda,i2c_port
DisplayPort *user_display = NULL;

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(WifiProvisioning_Bootstrap());

    Custom_PmicPortInit(&user_i2cbus,0x34);

    user_display = new DisplayPort(user_i2cbus,480,480);
    user_display->DisplayPort_TouchInit();
    ESP_ERROR_CHECK(Lvgl_PortInit(*user_display));

    if(Lvgl_lock(-1) == ESP_OK) {
        DesktopUI_Init(&user_i2cbus, user_display);
        Lvgl_unlock();
    }
}
