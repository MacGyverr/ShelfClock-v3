# ShelfClock

ShelfClock is an ESP32-powered seven-segment shelf display with clock, timer,
scheduler, lighting, music, sensor, weather, and audio-spectrum modes. Version
3 can be built as either a standalone PlatformIO application or an ESPHome
device with native Home Assistant controls. Both targets compile the same
ShelfClock runtime and use the same LittleFS webpages, songs, schedules, and
settings.

The current firmware is version `3.0.0-alpha`. Both targets have been compiled
and hardware-tested. The standalone firmware has also received repeated
regression testing throughout the 3.x refactor. The majority of the clock was 
once wrtten by a human, this refactor to add ESPHome options was done entirely 
by an LLM.

## Choose A Firmware

| Firmware | Network owner | Best for |
| --- | --- | --- |
| Standalone | ShelfClock and AutoConnect | Complete local operation without Home Assistant or ESPHome |
| ESPHome | ESPHome | Native Home Assistant entities, ESPHome OTA, Improv provisioning, and the same local ShelfClock interface |

These are separate firmware binaries built around one shared implementation.
The ESPHome build does not contain or launch the standalone binary.

## Install A Release

A complete release consists of four files. Each host build command creates its
matching pair:

| File | Purpose |
| --- | --- |
| `*-Full-Firmware.bin` | Complete standalone USB installation, including LittleFS |
| `*-OTA-Update.bin` | Standalone application-only update |
| `*-ESPHome-Full-Firmware.bin` | Complete ESPHome USB installation, including LittleFS |
| `*-OTA-ESPHome.bin` | ESPHome application-only update |

The build environments create these same neutral filenames.
The selected environment determines the embedded wiring map and default LED
count, but an installed device always presents itself as **ShelfClock**.
ShelfClock supports **one through ten LEDs per segment**.

Use a `Full-Firmware` image for a new device, a deliberately erased device, or
when switching between standalone and ESPHome. It is a complete 4 MB flash
image and replaces firmware, filesystem, settings, schedules, songs, and Wi-Fi
credentials.

Use the matching `OTA` image for routine updates. OTA preserves the filesystem,
settings, schedules, songs, and Wi-Fi credentials. Never send a
`Full-Firmware` image through ShelfClock's Update Firmware page.

See [Installing ShelfClock](docs/INSTALLING.md) for first boot, Wi-Fi setup,
OTA, PlatformIO upload behavior, and recovery instructions.

## Local Web Interface

After Wi-Fi setup, open the IP address shown by the clock. Depending on local
DNS support, `http://shelfclock.local` may also work. The web interface
provides:

- Clock, date, temperature, humidity, scrolling, scoreboard, timer, stopwatch,
  lightshow, spectrum, and display-off modes.
- Display colors, brightness, animations, sensors, weather, and clock settings.
- Daily, weekly, monthly, date, recurring, hourly, and special schedule types.
- RTTTL song testing, upload, deletion, and alarm selection.
- Four save/load presets that schedules can apply.
- Debug information, manual time synchronization, firmware update, and Wi-Fi
  recovery.


## Home Assistant

The ESPHome build exposes the display mode, timer duration and start actions,
scoreboard values, lightshows, spectrum modes, four preset save/load pairs, and
network recovery through the native ESPHome API. If Home Assistant does not
discover the clock automatically, add the ESPHome integration manually with
the clock's IP address or hostname.

All controls belong to one ShelfClock device, but Home Assistant decides how
entities appear on dashboards. To show them together, create an Entities card
and add the ShelfClock entities to it. 

See [ESPHome Builds](esphome/README.md) for YAML customization, provisioning,
ownership boundaries, and advanced builds.

## Building From Source

Choose a firmware host and hardware environment:

| Command | Result |
| --- | --- |
| `Build-Stand-alone.cmd` | Full-clock Stand-alone Full and OTA images |
| `Build-ESPHome.cmd` | Full-clock ESPHome Full and OTA images |

Each command writes its two neutral ShelfClock files under `Releases`.
The Test and Full environments use the same public filenames, so building one
replaces the prior files for that firmware host.

See [Building ShelfClock](BUILDING.md) for requirements, output names, ordinary
development builds, custom geometry, and why the two firmware hosts exist.

## Software Layout

| Path | Responsibility |
| --- | --- |
| `src/ShelfClock.cpp` | Shared clock behavior, display modes, web routes, settings, scheduler, and audio runtime |
| `src/main.cpp` and `src/StandaloneHost.*` | Standalone PlatformIO lifecycle, AutoConnect, DS3231, DHT, and network services |
| `lib/ShelfClockCore` | Shared commands, state, scheduling helpers, scrolling, and host interfaces |
| `components/shelfclock` | ESPHome lifecycle and Home Assistant adapter |
| `include` | Compile-time options, LED geometry, and display mappings |
| `data` | LittleFS webpages, JavaScript, styles, default settings, schedules, and RTTTL songs |
| `esphome` | Test/full YAML configurations and compile probes |
| `tools` | Release packaging, ESPHome environment, and verification tools |

Long-running scrolling, RTTTL playback, timers, spectrum input, and animated
effects use bounded runtime work so network and host services continue to run.
The standalone host retains the DS3231/NTP fallback policy. ESPHome owns time,
Wi-Fi, OTA, and DHT acquisition in its build while ShelfClock continues to own
the display, buzzer, files, schedules, and local web interface.

## Hardware

The reference clock uses an ESP32 DevKit, WS2812-compatible LEDs, DS3231 RTC,
DHT11, LDR, INMP441 microphone, and a transistor-driven buzzer. Most peripheral
features can be disabled at compile time. The full clock wiring map defaults to
seven LEDs per segment.

The `3D Printed Parts` and `diagrams` directories contain the existing
mechanical and wiring material. `ShelfClockv2-gerbers.zip` contains the current
reference PCB fabrication files.

The complete build can contain hundreds of LEDs and draw substantial current.
Use an appropriately sized supply, inject power along the display, use adequate
wire and connectors, and fuse the installation appropriately. The hardware is
a community project, not a certified electrical design.

ShelfClock began as a remix inspired by
[DIY Machines](https://www.youtube.com/c/DIYMachines/videos) and also drew
feature ideas from
[helpquick/7-Segment-WiFi-Clock](https://github.com/helpquick/7-Segment-WiFi-Clock).
The implementation and hardware have since diverged substantially from those
projects.

Contributions, hardware variants, testing reports, and documentation fixes are
welcome.
