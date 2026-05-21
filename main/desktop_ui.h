#pragma once

#include "i2c_bsp.h"
#include "display_bsp.h"

void DesktopUI_Init(I2cMasterBus *i2c_bus, DisplayPort *display);
