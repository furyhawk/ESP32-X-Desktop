# Repository Instructions

## Scope
This repository targets Waveshare's ESP32-C6-Touch-AMOLED-2.16 board. Keep changes aligned with the current split between the main app, reusable board support components.

## Build And Validate
- When changing the active app, validate with a full rebuild rather than editing build outputs.

## Project Layout
- [main/app/app_main.cpp](main/app/app_main.cpp) is the entrypoint and orchestration layer.
- [main/modules/desktop_ui/](main/modules/desktop_ui/) contains the LVGL desktop UI module.
- [main/modules/wifi_provisioning/](main/modules/wifi_provisioning/) contains Wi-Fi bootstrap, provisioning, and SNTP handling.
- [components/app_bsp/](components/app_bsp/), [components/port_bsp/](components/port_bsp/), [components/pmicpower/](components/pmicpower/), and [components/hw_platform/](components/hw_platform/) are the reusable hardware and board-support layers.
- [02_Example/](02_Example/) contains isolated example projects; do not mix example changes into the main app unless the user explicitly asks.

## Editing Conventions
- Update [main/CMakeLists.txt](main/CMakeLists.txt) when adding, moving, or removing source files in `main/`.
- Keep the root [CMakeLists.txt](CMakeLists.txt) workarounds for ESP-IDF v6 and managed-component warnings unless there is a verified replacement.
- Prefer small, module-scoped edits over broad rewrites.
- Preserve existing public function names unless a change is explicitly requested.

## Known Pitfalls
- ESP-IDF v6.0.1 moves `driver/i2c_master.h` under `components/esp_driver_i2c/include`; the root CMake workaround exposes that include path.
- Managed components may surface warnings that are intentionally suppressed at the root CMake level.
- Display rotation changes need the existing LVGL refresh path to avoid partial redraw artifacts.

## Hardware Platform Profiles
- Board-specific configuration lives in [components/hw_platform/profiles/](components/hw_platform/profiles/).
- The active profile is selected via Kconfig (`CONFIG_HW_PLATFORM_*`); default is `ESP32C6_AMOLED_480X480`.
- [components/hw_platform/hw_platform.h](components/hw_platform/hw_platform.h) is the single include for all `BSP_*` and `PLATFORM_*` macros.
- [main/user_config.h](main/user_config.h) is now a backward-compat shim that includes `hw_platform.h`.
- To add a new board: (1) add `profiles/<new_board>.h`, (2) add a `config` entry in `components/hw_platform/Kconfig`, (3) add the `#elif` branch in `hw_platform.h`.

## Reference Files
- [README.md](README.md)
- [CMakeLists.txt](CMakeLists.txt)
- [main/idf_component.yml](main/idf_component.yml)
- [main/user_config.h](main/user_config.h)
- [partitions.csv](partitions.csv)
