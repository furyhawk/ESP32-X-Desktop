/**
 * @file dummy_board.h
 * @brief Dummy/template hardware profile — copy this file to start a new board.
 *
 * Replace every GPIO_NUM_NC and placeholder value with the real pin assignments
 * for the target board, then:
 *   1. Add a config entry in components/hw_platform/Kconfig
 *   2. Add the matching #elif branch in components/hw_platform/hw_platform.h
 *   3. Set CONFIG_HW_PLATFORM_DUMMY_BOARD=y in sdkconfig.defaults (or via menuconfig)
 */
#pragma once

// ---------------------------------------------------------------------------
// Platform identity
// ---------------------------------------------------------------------------
#define PLATFORM_NAME                   "DUMMY_BOARD"

/** Display controller – set to whichever PLATFORM_DISPLAY_CTRL_* matches. */
#define PLATFORM_DISPLAY_CTRL_SH8601    1   // keep existing IDs for reference
#define PLATFORM_DISPLAY_CTRL           PLATFORM_DISPLAY_CTRL_SH8601

/** Touch controller */
#define PLATFORM_TOUCH_CTRL_CST9217     1
#define PLATFORM_TOUCH_CTRL             PLATFORM_TOUCH_CTRL_CST9217

/** PMIC (set to 0 / remove if no PMIC) */
#define PLATFORM_PMIC_AXP2101           1
#define PLATFORM_PMIC                   PLATFORM_PMIC_AXP2101
#define PLATFORM_PMIC_I2C_ADDR          (0x34)

/** IMU (set to 0 / remove if no IMU) */
#define PLATFORM_IMU_QMI8658            1
#define PLATFORM_IMU                    PLATFORM_IMU_QMI8658

// ---------------------------------------------------------------------------
// Feature flags  (set to 0 for peripherals not present on this board)
// ---------------------------------------------------------------------------
#define PLATFORM_HAS_TOUCH              1
#define PLATFORM_HAS_IMU                1
#define PLATFORM_HAS_PMIC               1
#define PLATFORM_HAS_AUDIO              0
#define PLATFORM_HAS_SD_CARD            0

// ---------------------------------------------------------------------------
// I2C bus
// ---------------------------------------------------------------------------
#define BSP_I2C_NUM                     (I2C_NUM_0)
#define BSP_I2C_SCL                     (GPIO_NUM_NC)   // TODO: set pin
#define BSP_I2C_SDA                     (GPIO_NUM_NC)   // TODO: set pin

// ---------------------------------------------------------------------------
// Display (QSPI)
// ---------------------------------------------------------------------------
#define BSP_LCD_SPI_NUM                 (SPI2_HOST)
#define BSP_LCD_H_RES                   (480)           // TODO: set resolution
#define BSP_LCD_V_RES                   (480)
#define BSP_LCD_BITS_PER_PIXEL          (16)
#define BSP_LCD_DMASIZE                 (BSP_LCD_H_RES * BSP_LCD_V_RES)

#define BSP_LCD_PCLK                    (GPIO_NUM_NC)   // TODO: set pin
#define BSP_LCD_DATA0                   (GPIO_NUM_NC)
#define BSP_LCD_DATA1                   (GPIO_NUM_NC)
#define BSP_LCD_DATA2                   (GPIO_NUM_NC)
#define BSP_LCD_DATA3                   (GPIO_NUM_NC)
#define BSP_LCD_CS                      (GPIO_NUM_NC)
#define BSP_LCD_BACKLIGHT               (GPIO_NUM_NC)
#define BSP_LCD_RST                     (GPIO_NUM_NC)

// ---------------------------------------------------------------------------
// Touch (I2C)
// ---------------------------------------------------------------------------
#define BSP_LCD_TOUCH_INT               (GPIO_NUM_NC)   // TODO: set pin
#define BSP_LCD_TOUCH_RST               (GPIO_NUM_NC)

// ---------------------------------------------------------------------------
// Audio (I2S) — only used when PLATFORM_HAS_AUDIO=1
// ---------------------------------------------------------------------------
#define BSP_I2S_MCLK                    (GPIO_NUM_NC)
#define BSP_I2S_SCLK                    (GPIO_NUM_NC)
#define BSP_I2S_LCLK                    (GPIO_NUM_NC)
#define BSP_I2S_DOUT                    (GPIO_NUM_NC)
#define BSP_I2S_DSIN                    (GPIO_NUM_NC)
#define BSP_POWER_AMP_IO                (GPIO_NUM_NC)

// ---------------------------------------------------------------------------
// SD card (SPI) — only used when PLATFORM_HAS_SD_CARD=1
// ---------------------------------------------------------------------------
#define BSP_SD_CLK                      (GPIO_NUM_NC)
#define BSP_SD_MOSI                     (GPIO_NUM_NC)
#define BSP_SD_MISO                     (GPIO_NUM_NC)
#define BSP_SD_CS                       (GPIO_NUM_NC)
