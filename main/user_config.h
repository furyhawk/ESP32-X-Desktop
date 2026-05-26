/**
 * @file user_config.h
 * @brief Legacy shim – all hardware configuration is now defined by the
 *        selected platform profile in components/hw_platform/.
 *
 * Existing code that includes "user_config.h" continues to work unchanged.
 * New code should prefer:  #include "hw_platform.h"
 */
#ifndef USER_CONFIG_H
#define USER_CONFIG_H

#include "hw_platform.h"

#endif // !USER_CONFIG_H