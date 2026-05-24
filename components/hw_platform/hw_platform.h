/**
 * @file hw_platform.h
 * @brief Hardware platform abstraction – includes the active profile selected
 *        via Kconfig (CONFIG_HW_PLATFORM_*).
 *
 * Usage:
 *   #include "hw_platform.h"
 *
 * All BSP_* pin/resolution macros and PLATFORM_* capability flags are then
 * available regardless of which board is targeted.
 *
 * To add a new platform:
 *   1. Create  components/hw_platform/profiles/<new_board>.h
 *   2. Add a config entry in components/hw_platform/Kconfig
 *   3. Add the corresponding #elif branch below
 */
#pragma once

#if defined(CONFIG_HW_PLATFORM_ESP32C6_AMOLED_480X480)
    #include "profiles/esp32c6_amoled_480x480.h"
#elif defined(CONFIG_HW_PLATFORM_DUMMY_BOARD)
    #include "profiles/dummy_board.h"
#else
    #error "No hardware platform selected. Set CONFIG_HW_PLATFORM_* in sdkconfig."
#endif
