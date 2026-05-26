# ESP32-C6-Touch-AMOLED-2.16

中文 Wiki: https://www.waveshare.net/wiki/ESP32-C6-Touch-AMOLED-2.16  
English Wiki: https://www.waveshare.com/wiki/ESP32-C6-Touch-AMOLED-2.16

## Overview

This repository contains ESP-IDF firmware and examples for the ESP32-C6 Touch AMOLED 2.16 board.

- Root firmware is an LVGL-based desktop UI project.
- Additional examples are provided under 02_Example, from basic peripheral demos to UI and networking demos.

## Arduino Tools Configuration

![Arduino Tools Configuration](Tools%20Configuration.png)

## Environment

- ESP-IDF: v6.0.1 recommended
- Target chip: esp32c6
- Host OS: macOS, Linux, or Windows

## Quick Start (ESP-IDF)

1. Clone repository:

```bash
git clone https://github.com/furyhawk/ESP32-X-Desktop.git
cd ESP32-X-Desktop
```

2. Set up ESP-IDF environment and target:

```bash
. $IDF_PATH/export.sh
idf.py set-target esp32c6
```

3. Build firmware:

```bash
idf.py build
```

If your local ESP-IDF Python environment has issues with idf.py build, use the generated build directory directly:

```bash
cmake --build build -j4
```

4. Flash and monitor:

```bash
idf.py -p /dev/tty.usbmodemXXX flash monitor
```

## Project Layout

- 03_Firmware: generated build artifacts and internal build outputs
- components: board support, platform abstraction, and reusable modules
- main: application entry, UI app code, modules, and configuration headers
- 02_Example: standalone example projects
- scripts: utility scripts for flashing and diagnostics

## Examples

Examples are located in 02_Example:

- 01_AXP2101_Test
- 02_I2C_QMI8658
- 03_I2C_PCF85063
- 04_SD_Card
- 05_WIFI_STA
- 06_WIFI_AP
- 07_Audio_Test
- 08_LVGL_V8_Test
- 09_LVGL_V9_Test

Each example includes its own CMakeLists.txt and sdkconfig.defaults.

## Notes

- Root project currently uses project(09_LVGL_V9_Test) in top-level CMakeLists.txt.
- Build configuration files include sdkconfig, sdkconfig.defaults, and partitions.csv.

## Change Log

### [1.0.0] 2026/03/30

- Initial version
- Examples progress from simple to advanced, helping users get started quickly

## 更新日志

### [1.0.0] 2026/03/30

- 初始版本
- 示例由浅入深，帮助用户快速上手
