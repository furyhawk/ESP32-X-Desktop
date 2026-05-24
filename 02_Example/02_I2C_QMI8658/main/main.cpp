
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <freertos/FreeRTOS.h>
#include <esp_log.h>

#include "i2c_bsp.h"
#include "display_bsp.h"
#include "power_bsp.h"
#include "user_config.h"
#include "qmi8658.h"

I2cMasterBus I2cMasterBus_(BSP_I2C_SCL,BSP_I2C_SDA,BSP_I2C_NUM);
static DisplayPort *user_display = NULL;
static qmi8658_dev_t qmi8658;       	
static char LvglDataBuff[40] = {""}; 	
static uint8_t current_rotation = 0;
static constexpr uint32_t QMI8658_TASK_STACK_SIZE = 6 * 1024;
static constexpr UBaseType_t QMI8658_STACK_WARN_WORDS = 256;

static bool qmi8658_detect_rotation(const qmi8658_data_t *sensor_data, uint8_t *new_rotation)
{
    const float min_tilt_mps2 = 6.5f;
    const float axis_margin_mps2 = 1.5f;

    float abs_x = fabsf(sensor_data->accelX);
    float abs_y = fabsf(sensor_data->accelY);

    if(abs_x > min_tilt_mps2 && abs_x > (abs_y + axis_margin_mps2)) {
        *new_rotation = 1;
        return true;
    }

    if(abs_y > min_tilt_mps2 && abs_y > (abs_x + axis_margin_mps2)) {
        *new_rotation = 0;
        return true;
    }

    return false;
}

void QMI8658_Task(void *arg) {
    (void)arg;
	uint8_t pending_rotation = current_rotation;
	int stable_samples = 0;
	const int samples_required = 3;
    uint32_t loop_count = 0;

	while(1) {
        bool ready;
        int ret = qmi8658_is_data_ready(&qmi8658, &ready);
        if(ret == ESP_OK && ready) {
            qmi8658_data_t qmidata = {};
            ret = qmi8658_read_sensor_data(&qmi8658, &qmidata);
            if(ret == ESP_OK) {
                snprintf(LvglDataBuff, sizeof(LvglDataBuff), "Acc(m/s2):%.2f,%.2f,%.2f",\
                qmidata.accelX, qmidata.accelY, qmidata.accelZ);
                ESP_LOGW("acc","%s", LvglDataBuff);
                snprintf(LvglDataBuff, sizeof(LvglDataBuff), "Gyro(rad/s):%.2f,%.2f,%.2f",\
                qmidata.gyroX, qmidata.gyroY, qmidata.gyroZ);
                ESP_LOGW("gyro","%s", LvglDataBuff);

                uint8_t detected_rotation = current_rotation;
                if(qmi8658_detect_rotation(&qmidata, &detected_rotation) && detected_rotation != current_rotation) {
                    if(detected_rotation == pending_rotation) {
                        stable_samples++;
                    } else {
                        pending_rotation = detected_rotation;
                        stable_samples = 1;
                    }

                    if(stable_samples >= samples_required && user_display != NULL) {
                        current_rotation = detected_rotation;
                        user_display->Set_Rotate(current_rotation);
                        ESP_LOGI("display", "Screen rotation changed to %s", current_rotation ? "landscape" : "portrait");
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
                ESP_LOGW("qmi8658", "Task low stack watermark: %u words", (unsigned)free_words);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(150));
	}
}

extern "C" void app_main(void) {
    Custom_PmicPortInit(&I2cMasterBus_,0x34);

    user_display = new DisplayPort(I2cMasterBus_,
                                   BSP_LCD_H_RES,
                                   BSP_LCD_V_RES,
                                   BSP_LCD_PCLK,
                                   BSP_LCD_DATA0,
                                   BSP_LCD_DATA1,
                                   BSP_LCD_DATA2,
                                   BSP_LCD_DATA3,
                                   BSP_LCD_CS,
                                   BSP_LCD_TOUCH_INT,
                                   BSP_LCD_TOUCH_RST,
                                   BSP_LCD_SPI_NUM);
    user_display->Set_Backlight(100);
    user_display->Set_Rotate(current_rotation);

    esp_err_t ret = qmi8658_init(&qmi8658, I2cMasterBus_.Get_I2cBusHandle(), QMI8658_ADDRESS_HIGH);
    if (ret != ESP_OK) {
        ESP_LOGE("qmi8658", "Failed to initialize QMI8658 (error: %d)", ret);
    } else {
        qmi8658_set_accel_range(&qmi8658, QMI8658_ACCEL_RANGE_8G);
        qmi8658_set_accel_odr(&qmi8658, QMI8658_ACCEL_ODR_1000HZ);
        qmi8658_set_gyro_range(&qmi8658, QMI8658_GYRO_RANGE_512DPS);
        qmi8658_set_gyro_odr(&qmi8658, QMI8658_GYRO_ODR_1000HZ);
        qmi8658_set_accel_unit_mps2(&qmi8658, true); 
        qmi8658_set_gyro_unit_rads(&qmi8658, true);  
        qmi8658_set_display_precision(&qmi8658, 4);  
    }
    xTaskCreatePinnedToCore(QMI8658_Task, "QMI8658_Task", QMI8658_TASK_STACK_SIZE, NULL, 2, NULL,0);
}
