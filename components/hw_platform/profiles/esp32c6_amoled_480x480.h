/**
 * @file esp32c6_amoled_480x480.h
 * @brief Hardware profile: Waveshare ESP32-C6-Touch-AMOLED-2.16 (480×480)
 *
 * Display controller : SH8601 (QSPI)
 * Touch controller   : CST9217 (I2C)
 * PMIC               : AXP2101 (I2C, 0x34)
 * IMU                : QMI8658 (I2C)
 */
#pragma once

// ---------------------------------------------------------------------------
// Platform identity
// ---------------------------------------------------------------------------
#define PLATFORM_NAME                   "ESP32-C6-AMOLED-2.16"

/** Display controller identifiers – extend as new controllers are added. */
#define PLATFORM_DISPLAY_CTRL_SH8601    1
#define PLATFORM_DISPLAY_CTRL           PLATFORM_DISPLAY_CTRL_SH8601

/** Touch controller identifiers */
#define PLATFORM_TOUCH_CTRL_CST9217     1
#define PLATFORM_TOUCH_CTRL             PLATFORM_TOUCH_CTRL_CST9217

/** PMIC identifiers */
#define PLATFORM_PMIC_AXP2101           1
#define PLATFORM_PMIC                   PLATFORM_PMIC_AXP2101
#define PLATFORM_PMIC_I2C_ADDR          (0x34)

/** IMU identifiers */
#define PLATFORM_IMU_QMI8658            1
#define PLATFORM_IMU                    PLATFORM_IMU_QMI8658

// ---------------------------------------------------------------------------
// Feature flags
// ---------------------------------------------------------------------------
#define PLATFORM_HAS_TOUCH              1
#define PLATFORM_HAS_IMU                1
#define PLATFORM_HAS_PMIC               1
#define PLATFORM_HAS_AUDIO              1
#define PLATFORM_HAS_SD_CARD            1

// ---------------------------------------------------------------------------
// I2C bus
// ---------------------------------------------------------------------------
#define BSP_I2C_NUM                     (I2C_NUM_0)
#define BSP_I2C_SCL                     (GPIO_NUM_7)
#define BSP_I2C_SDA                     (GPIO_NUM_8)

// ---------------------------------------------------------------------------
// Display (QSPI / SH8601)
// ---------------------------------------------------------------------------
#define BSP_LCD_SPI_NUM                 (SPI2_HOST)
#define BSP_LCD_H_RES                   (480)
#define BSP_LCD_V_RES                   (480)
#define BSP_LCD_BITS_PER_PIXEL          (16)
#define BSP_LCD_DMASIZE                 (BSP_LCD_H_RES * BSP_LCD_V_RES)

#define BSP_LCD_PCLK                    (GPIO_NUM_0)
#define BSP_LCD_DATA0                   (GPIO_NUM_1)
#define BSP_LCD_DATA1                   (GPIO_NUM_2)
#define BSP_LCD_DATA2                   (GPIO_NUM_3)
#define BSP_LCD_DATA3                   (GPIO_NUM_4)
#define BSP_LCD_CS                      (GPIO_NUM_5)
#define BSP_LCD_BACKLIGHT               (GPIO_NUM_NC)
#define BSP_LCD_RST                     (GPIO_NUM_NC)

// ---------------------------------------------------------------------------
// Touch (CST9217 via I2C)
// ---------------------------------------------------------------------------
#define BSP_LCD_TOUCH_INT               (GPIO_NUM_15)
#define BSP_LCD_TOUCH_RST               (GPIO_NUM_11)

// ---------------------------------------------------------------------------
// Audio (I2S)
// ---------------------------------------------------------------------------
#define BSP_I2S_MCLK                    (GPIO_NUM_19)
#define BSP_I2S_SCLK                    (GPIO_NUM_20)
#define BSP_I2S_LCLK                    (GPIO_NUM_22)
#define BSP_I2S_DOUT                    (GPIO_NUM_23)
#define BSP_I2S_DSIN                    (GPIO_NUM_21)
#define BSP_POWER_AMP_IO                (GPIO_NUM_NC)

// ---------------------------------------------------------------------------
// SD card (SPI, shared QSPI data lines when display is idle)
// ---------------------------------------------------------------------------
#define BSP_SD_CLK                      (GPIO_NUM_0)
#define BSP_SD_MOSI                     (GPIO_NUM_1)
#define BSP_SD_MISO                     (GPIO_NUM_2)
#define BSP_SD_CS                       (GPIO_NUM_6)
