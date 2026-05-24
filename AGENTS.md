# Repository Instructions

## Scope
This repository targets Waveshare's ESP32-C6-Touch-AMOLED-2.16 board. Keep changes aligned with the current split between the main app, reusable board support components, and the self-contained examples under [02_Example/](02_Example/).

## Build And Validate
- Prefer `cmake --build build -j4` from the repo root for validation.
- Treat `idf.py build` as unreliable in this environment unless the Python/ESP-IDF setup has been fixed.
- When changing the active app, validate with a full rebuild rather than editing build outputs.

## Project Layout
- [main/app/app_main.cpp](main/app/app_main.cpp) is the entrypoint and orchestration layer.
- [main/modules/desktop_ui/](main/modules/desktop_ui/) contains the LVGL desktop UI module.
- [main/modules/wifi_provisioning/](main/modules/wifi_provisioning/) contains Wi-Fi bootstrap, provisioning, and SNTP handling.
- [components/app_bsp/](components/app_bsp/), [components/port_bsp/](components/port_bsp/), and [components/pmicpower/](components/pmicpower/) are the reusable hardware and board-support layers.
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

## Reference Files
- [README.md](README.md)
- [CMakeLists.txt](CMakeLists.txt)
- [main/idf_component.yml](main/idf_component.yml)
- [main/user_config.h](main/user_config.h)
- [partitions.csv](partitions.csv)
