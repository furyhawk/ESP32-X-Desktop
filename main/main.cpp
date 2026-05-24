
#include <esp_log.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "lvgl_bsp.h"
#include "power_bsp.h"
#include "desktop_ui.h"
#include "wifi_provisioning.h"
#include "qmi8658.h"

#define TAG "main"

I2cMasterBus user_i2cbus(7,8,0); //scl,sda,i2c_port
DisplayPort *user_display = NULL;
static qmi8658_dev_t qmi8658;
static uint8_t current_rotation = 0;
static constexpr uint32_t QMI8658_TASK_STACK_SIZE = 6 * 1024;
static constexpr UBaseType_t QMI8658_STACK_WARN_WORDS = 256;
static constexpr uint8_t SENSOR_ROTATION_OFFSET = 1; // +90 degrees clockwise (mod 2 for 0/90 states)

static bool qmi8658_detect_rotation(const qmi8658_data_t *sensor_data, uint8_t *new_rotation)
{
    const float min_tilt_mps2 = 6.5f;
    const float axis_margin_mps2 = 1.5f;

    float abs_x = (sensor_data->accelX < 0.0f) ? -sensor_data->accelX : sensor_data->accelX;
    float abs_y = (sensor_data->accelY < 0.0f) ? -sensor_data->accelY : sensor_data->accelY;

    if(abs_x > min_tilt_mps2 && abs_x > (abs_y + axis_margin_mps2)) {
        *new_rotation = (1 + SENSOR_ROTATION_OFFSET) & 0x01;
        return true;
    }

    if(abs_y > min_tilt_mps2 && abs_y > (abs_x + axis_margin_mps2)) {
        *new_rotation = (0 + SENSOR_ROTATION_OFFSET) & 0x01;
        return true;
    }

    return false;
}

static void qmi8658_orientation_task(void *arg)
{
    (void)arg;

    uint8_t pending_rotation = current_rotation;
    int stable_samples = 0;
    const int samples_required = 3;
    uint32_t loop_count = 0;

    while(1) {
        bool ready = false;
        int ret = qmi8658_is_data_ready(&qmi8658, &ready);
        if(ret == ESP_OK && ready) {
            qmi8658_data_t sensor_data = {};
            ret = qmi8658_read_sensor_data(&qmi8658, &sensor_data);
            if(ret == ESP_OK) {
                uint8_t detected_rotation = current_rotation;
                if(qmi8658_detect_rotation(&sensor_data, &detected_rotation) && detected_rotation != current_rotation) {
                    if(detected_rotation == pending_rotation) {
                        stable_samples++;
                    } else {
                        pending_rotation = detected_rotation;
                        stable_samples = 1;
                    }

                    if(stable_samples >= samples_required && user_display != NULL) {
                        current_rotation = detected_rotation;
                        if(Lvgl_lock(200) == ESP_OK) {
                            user_display->Set_Rotate(current_rotation);
                            Lvgl_HandleRotationChange();
                            Lvgl_unlock();
                        } else {
                            user_display->Set_Rotate(current_rotation);
                        }
                        ESP_LOGI(TAG, "Screen rotation changed to %s", current_rotation ? "landscape" : "portrait");
                        stable_samples = 0;
                    }
                } else {
                    stable_samples = 0;
                    pending_rotation = current_rotation;
                }
            }
        }

        loop_count++;
        if((loop_count % 100) == 0) {
            UBaseType_t free_words = uxTaskGetStackHighWaterMark(NULL);
            if(free_words < QMI8658_STACK_WARN_WORDS) {
                ESP_LOGW(TAG, "qmi8658 task low stack watermark: %u words", (unsigned)free_words);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(150));
    }
}

extern "C" void app_main(void)
{
    Custom_PmicPortInit(&user_i2cbus,0x34);

    user_display = new DisplayPort(user_i2cbus,480,480);
    user_display->DisplayPort_TouchInit();
    user_display->Set_Rotate(current_rotation);
    ESP_ERROR_CHECK(Lvgl_PortInit(*user_display));

    if(Lvgl_lock(-1) == ESP_OK) {
        DesktopUI_Init(&user_i2cbus, user_display);
        Lvgl_unlock();
    }

    ESP_ERROR_CHECK(WifiProvisioning_Bootstrap());

    esp_err_t ret = qmi8658_init(&qmi8658, user_i2cbus.Get_I2cBusHandle(), QMI8658_ADDRESS_HIGH);
    if(ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize QMI8658 (error: %d)", ret);
    } else {
        ESP_ERROR_CHECK(qmi8658_set_accel_range(&qmi8658, QMI8658_ACCEL_RANGE_8G));
        ESP_ERROR_CHECK(qmi8658_set_accel_odr(&qmi8658, QMI8658_ACCEL_ODR_1000HZ));
        ESP_ERROR_CHECK(qmi8658_set_gyro_range(&qmi8658, QMI8658_GYRO_RANGE_512DPS));
        ESP_ERROR_CHECK(qmi8658_set_gyro_odr(&qmi8658, QMI8658_GYRO_ODR_1000HZ));
        qmi8658_set_accel_unit_mps2(&qmi8658, true);
        qmi8658_set_gyro_unit_rads(&qmi8658, true);
        qmi8658_set_display_precision(&qmi8658, 4);
        xTaskCreatePinnedToCore(qmi8658_orientation_task, "qmi8658_orientation", QMI8658_TASK_STACK_SIZE, NULL, 2, NULL, 0);
    }

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
