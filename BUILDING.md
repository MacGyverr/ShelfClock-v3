# Building ShelfClock

ShelfClock has one active PlatformIO environment and two release commands. Edit
the shared feature flags, then build either the Standalone or ESPHome host around
the same clock implementation.

## Build Commands

Run one command from the project root:

| Command | Files created in `Releases` |
| --- | --- |
| `Build-ShelfClock-Standalone.cmd` | `ShelfClock-Full-Firmware.bin`, `ShelfClock-OTA-Update.bin` |
| `Build-ShelfClock-ESPHome.cmd` | `ShelfClock-ESPHome-Full-Firmware.bin`, `ShelfClock-ESPHome-OTA-Update.bin` |

Each command creates both a complete USB image and an application-only OTA
image. It prints the selected environment, wiring map, LEDs per segment,
network flavor, flash layout, and enabled hardware before compiling.

The ESPHome command installs its pinned local toolchain on first use.
`SHELFCLOCK_ISOLATION=true` causes that command to stop with a clear error
because a permanent isolated access point cannot provide ESPHome or Home
Assistant connectivity.

## Select Features

All user-selectable options are under `[env]` in `platformio.ini`. Comments
above each flag describe its effect:

- `TEST_CLOCK=false` selects the full wiring map; `true` selects the test
  clock mapping.
- `SHELFCLOCK_ISOLATION=true` creates the permanent
  `ShelfClock-XXXXXX` access point at `10.10.10.10`. Disable both weather
  flags for this build.
- `HAS_ONLINEWEATHER` and `HAS_USWEATHER` control internet weather
  features.
- `HAS_RTC`, `HAS_DHT`, `HAS_SOUNDDETECTOR`, `HAS_BUZZER`, and
  `HAS_PHOTOSENSOR` control the corresponding hardware.
- `LEDS_PER_SEGMENT` may optionally override the automatic value. Supported
  values are 1 through 10; otherwise Test uses 4 and Full uses 7.

Both release commands read the same effective flags. ESPHome adds only the host
and FastLED transport settings required by its framework.

## Hardware Environments

`[env:espwroom32]` describes the tested ESP-WROOM-32 with 4 MB flash. It owns
the board, partition table, flash size, and release-image offsets. The release
packager checks that its filesystem metadata agrees with the partition CSV.

The scripts build the one environment named by `platformio.default_envs`.
Future hardware flavors can inherit the common `[env]` flags:

```ini
[platformio]
default_envs = espwroom32_8m

[env:espwroom32_8m]
board = esp32dev
board_upload.flash_size = 8MB
board_build.flash_size = 8MB
board_build.filesystem = littlefs
board_build.partitions = 8M-partitions.csv

custom_flash_size = 8MB
custom_flash_bytes = 0x800000
custom_bootloader_offset = 0x1000
custom_partitions_offset = 0x8000
custom_boot_app_offset = 0xE000
custom_application_offset = 0x10000
custom_filesystem_offset = <offset from the partition CSV>
custom_filesystem_size = <size from the partition CSV>
```

An 8 MB environment is not included because it needs a deliberate, tested
partition layout. Do not reuse the 4 MB filesystem offset for another layout.

## Release Images

Complete images contain the bootloader, partition table, application, and
LittleFS webpages, songs, schedules, and defaults. They are padded to the flash
size declared by the active environment. Installing one replaces all flash
data.

OTA images contain only the application. They preserve LittleFS, settings,
schedules, songs, and network credentials. Never upload a Full Firmware image
through ShelfClock's Update Firmware page.

## USB Installation

After building Standalone firmware, run:

```text
Upload-Full-Firmware-via-USB.cmd
```

The uploader runs `Get-PnpDevice -Class Ports` to show Windows device records,
then uses the active serial-port list for its numbered choices. Enter only the
bracketed menu number. Devices with `Unknown` status and `Present=False` are
remembered entries and cannot be flashed until Windows exposes an active COM
port. The uploader requires typing `ERASE` before writing
`ShelfClock-Full-Firmware.bin` at flash offset `0x0`.

An active port can also be supplied when launching the command:

```powershell
.\Upload-Full-Firmware-via-USB.cmd -Port COM8
```

The hosted [ShelfClock installer](esphome/installer.html) offers complete
Standalone and ESPHome images through ESP Web Tools. It must be served from
HTTPS or localhost and downloads binaries attached to the latest GitHub
release.

## PlatformIO Tasks

The ordinary PlatformIO **Build** and **Upload** actions operate on only the
application at `.pio\build\espwroom32\firmware.bin`. The **Build Filesystem
Image** and **Upload Filesystem Image** actions operate on LittleFS separately.

Therefore a fresh device can be installed in either of these ways:

1. Run PlatformIO **Upload**, then **Upload Filesystem Image**.
2. Run a release command and install its merged Full Firmware image.

The standard PlatformIO Upload action intentionally remains application-only.

## Requirements

- Windows 10 or 11.
- VS Code with the PlatformIO extension.
- Python 3.12 for ESPHome builds.
- Several gigabytes of free space for the first ESPHome toolchain installation.

Generated `.pio`, `.cache`, `.tools`, `dist`, and `Releases` content
is ignored by Git.

## Shared Implementation

PlatformIO compiles `src/main.cpp` with the Standalone host. ESPHome loads
`components/shelfclock` and compiles the same `src/ShelfClock.cpp`, project
headers, and `lib/ShelfClockCore`. Neither firmware embeds the other host's
binary.

Preserve the intentional scheduler sentinels `99` and `9999`, and keep the
distinct `FAKE_LEDs_C_VERT` and `FAKE_LEDs_C_VERT2` mappings when creating
hardware variants.
