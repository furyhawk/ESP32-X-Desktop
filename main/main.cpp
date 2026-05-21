
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
    Custom_PmicPortInit(&user_i2cbus,0x34);

    user_display = new DisplayPort(user_i2cbus,480,480);
    user_display->DisplayPort_TouchInit();
    ESP_ERROR_CHECK(Lvgl_PortInit(*user_display));

    if(Lvgl_lock(-1) == ESP_OK) {
        DesktopUI_Init(&user_i2cbus, user_display);
        Lvgl_unlock();
    }

    ESP_ERROR_CHECK(WifiProvisioning_Bootstrap());

    if(WifiProvisioning_IsProvisioning()) {
        char svc_name[20]    = {0};
        char qr_payload[200] = {0};
        WifiProvisioning_GetServiceName(svc_name, sizeof(svc_name));
        WifiProvisioning_GetQRPayload(qr_payload, sizeof(qr_payload));
        if(Lvgl_lock(-1) == ESP_OK) {
            DesktopUI_ShowProvisioningQR(svc_name, "prov1234", qr_payload);
            Lvgl_unlock();
        }
    }
}
