# Building ShelfClock

The repository provides five Windows build commands. Run them from the project
root. They compile and package release files but never upload anything to a
clock.

## Choose A Host

ShelfClock has two firmware hosts around the same clock implementation:

- **Stand-alone** owns Wi-Fi setup, DS3231 and DHT access, the local web
  interface, and browser-based OTA without requiring ESPHome or Home Assistant.
- **ESPHome** adds native Home Assistant entities, ESPHome provisioning and
  OTA, and ESPHome-owned time and sensor services while retaining the local
  ShelfClock interface.

Choose one host for a device. Switching hosts requires a complete USB install.
The Stand-alone host also has an **Isolation** build that creates a permanent
local access point and never joins a router. It intentionally has no ESPHome
counterpart because ESPHome and Home Assistant require network connectivity.
Its access point is `ShelfClock-XXXXXX`, its password is `shelfclock`, and the
local interface has the fixed address `http://10.10.10.10`.

## Choose A Hardware Build

Each host has two developer build environments:

- **Full** uses the full shelf-light wiring map and defaults to seven LEDs per
  segment.
- **Test** uses the smaller development-clock wiring map and defaults to four
  LEDs per segment.

The wiring map matters in addition to the LED count. ShelfClock supports one
through ten LEDs per segment, but changing only the count does not change the
physical shelf-light mapping.

The Test label is intentionally limited to source build commands. Release
filenames, device names, hostnames, Home Assistant, and the web interface all
identify the finished device simply as **ShelfClock**.

## Build Commands

| Command | Firmware host | Hardware environment | Files created |
| --- | --- | --- | --- |
| `Build-Stand-alone.cmd` | Stand-alone | Full | `ShelfClock-Full-Firmware.bin`, `ShelfClock-OTA-Update.bin` |
| `Build-Stand-alone-Test.cmd` | Stand-alone | Test | `ShelfClock-Full-Firmware.bin`, `ShelfClock-OTA-Update.bin` |
| `Build-Stand-alone-Isolation.cmd` | Stand-alone | Full, isolated network | `ShelfClock-Isolation-Full-Firmware.bin`, `ShelfClock-Isolation-OTA-Update.bin` |
| `Build-ESPHome.cmd` | ESPHome | Full | `ShelfClock-ESPHome-Full-Firmware.bin`, `ShelfClock-OTA-ESPHome.bin` |
| `Build-ESPHome-Test.cmd` | ESPHome | Test | `ShelfClock-ESPHome-Full-Firmware.bin`, `ShelfClock-OTA-ESPHome.bin` |

All files are written to `Releases`. Full and Test commands for the same host
use the same public filenames, so the most recently built environment replaces
that host's previous two files. The console states which environment produced
them. Isolation uses unique filenames and does not replace the normal
Stand-alone pair.

Each command rebuilds the cleaned LittleFS content and excludes the deliberately
invalid RTTTL parser fixture.

## Release File Types

| File | Use |
| --- | --- |
| `ShelfClock-Full-Firmware.bin` | Complete new Stand-alone installation |
| `ShelfClock-OTA-Update.bin` | Later Stand-alone firmware update |
| `ShelfClock-Isolation-Full-Firmware.bin` | Complete new isolated Stand-alone installation |
| `ShelfClock-Isolation-OTA-Update.bin` | Later isolated Stand-alone firmware update |
| `ShelfClock-ESPHome-Full-Firmware.bin` | Complete new ESPHome installation |
| `ShelfClock-OTA-ESPHome.bin` | Later ESPHome firmware update |

Complete images are exactly 4 MB and contain firmware plus LittleFS webpages,
songs, schedules, and default settings. OTA images contain only the application
and preserve the filesystem, settings, songs, schedules, and Wi-Fi data.

Never upload a Full-Firmware image through the device's OTA page. See
[`docs/INSTALLING.md`](docs/INSTALLING.md) for flashing and recovery details.

## Requirements

- Windows 10 or 11.
- VS Code with the PlatformIO extension.
- Python 3.12 for ESPHome builds.
- Several gigabytes of free space for the first ESPHome toolchain installation.

PlatformIO dependencies are pinned in `platformio.ini`. The ESPHome commands
automatically create `.tools\esphome` from
`tools\requirements-esphome.txt` the first time they run. Generated
`.pio`, `.cache`, `.tools`, `dist`, and `Releases` content is ignored by
Git.

## Ordinary Development Builds

Developers can compile a Stand-alone application without packaging:

```text
platformio run -e espwroom32
platformio run -e espwroom32_test
platformio run -e espwroom32_isolation
```

The resulting application is
`.pio\build\<environment>\firmware.bin`. PlatformIO's ordinary **Upload**
task writes only that application. A new device installed this way also needs
PlatformIO's **Upload Filesystem Image** task.

Advanced ESPHome compile checks can be run after an ESPHome build has created
the local environment:

```powershell
& .\.tools\esphome\Scripts\python.exe tools\run_esphome_probe.py --target runtime --variant full
& .\.tools\esphome\Scripts\python.exe tools\run_esphome_probe.py --target runtime --variant test
```

The runner writes generated ESPHome state under `.cache\esphome` and never
uploads firmware.

## Custom Hardware

PlatformIO geometry and feature defaults are in
`include/ShelfClockConfig.h` and can be overridden with build flags in
`platformio.ini`. ESPHome uses the `leds_per_segment` and `test_clock`
substitutions in its YAML files.

Preserve the intentional scheduler sentinels `99` and `9999`, and keep the
distinct `FAKE_LEDs_C_VERT` and `FAKE_LEDs_C_VERT2` mappings when creating
hardware variants.

## Shared Implementation

PlatformIO compiles `src/main.cpp` with the Stand-alone host. ESPHome reads
`esphome/shelfclock-full.yaml` or `esphome/shelfclock-test.yaml` and loads
`components/shelfclock`.

Both hosts compile the authoritative `src/ShelfClock.cpp`, project headers,
and `lib/ShelfClockCore`. Neither firmware embeds the other host's binary.
