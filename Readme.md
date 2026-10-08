# ShelfClock Firmware v3

<img width="2483" height="1765" alt="20211224_210619" src="https://github.com/user-attachments/assets/53944284-902c-4fc3-b0de-e370500cd475" />

ShelfClock is an ESP32-powered seven-segment shelf display with clock, timer,
scheduler, lighting, music, sensor, weather, and audio-spectrum modes. Version
3 can be built as either a standalone PlatformIO application or an ESPHome
device with native Home Assistant controls. Both targets compile the same
ShelfClock runtime and use the same LittleFS webpages, songs, schedules, and
settings.
3D printable files and instructions are available here:
https://www.thingiverse.com/thing:5100866
and here:
https://www.printables.com/model/201156-shelf-clock

Examples of it in action on the original firmware version can be found here:
https://www.youtube.com/watch?v=FABxgoq68Fs&list=PLkV6jp60iXvjmrqkrMTHSjlMShLybMzkE

This current firmware is version `3.0.2`. This refactor was completely done by ChatGPT. Both targets have been compile-validated,
and their shared runtime has been hardware-tested. The standalone firmware has also received repeated
regression testing throughout the 3.x refactor.

## Choose A Firmware

| Firmware | Network owner | Best for |
| --- | --- | --- |
| Standalone | ShelfClock and AutoConnect | Complete local operation without Home Assistant or ESPHome |
| Standalone Isolation | ShelfClock permanent access point | Direct local control where the clock must not join the surrounding network |
| ESPHome | ESPHome | Native Home Assistant entities, ESPHome OTA, Improv provisioning, and the same local ShelfClock interface |

These are separate firmware binaries built around one shared implementation.
The ESPHome build does not contain or launch the standalone binary.

## Install A Release

The Standalone and ESPHome build commands each create a complete image and an
application-only OTA image:

| File | Purpose |
| --- | --- |
| `ShelfClock-Full-Firmware.bin` | Complete Standalone USB installation, including LittleFS |
| `ShelfClock-OTA-Update.bin` | Standalone application-only update |
| `ShelfClock-ESPHome-Full-Firmware.bin` | Complete ESPHome USB installation, including LittleFS |
| `ShelfClock-ESPHome-OTA-Update.bin` | ESPHome application-only update |

The flags in `platformio.ini` determine the wiring map, isolation behavior,
weather support, and installed hardware. The output names stay stable because
the person compiling the firmware selected those flags. ShelfClock supports
**one through ten LEDs per segment**.

Use a `Full-Firmware` image for a new device, a deliberately erased device, or
when switching between standalone and ESPHome. It fills the flash size selected
by the active hardware environment and replaces firmware, filesystem, settings,
schedules, songs, and Wi-Fi credentials.

Use the matching `OTA` image for routine updates. OTA preserves the filesystem,
settings, schedules, songs, and Wi-Fi credentials. Never send a
`Full-Firmware` image through ShelfClock's Update Firmware page.

See [Installing ShelfClock](docs/INSTALLING.md) for first boot, Wi-Fi setup,
OTA, PlatformIO upload behavior, and recovery instructions.

## Local Web Interface

<img width="1440" height="8104" alt="Screenshot_20261007_172330_Brave" src="https://github.com/user-attachments/assets/8e200937-f118-4693-a472-ccb4906a858c" />

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

The Standalone Isolation build instead creates a permanent
`ShelfClock-XXXXXX` access point. Connect with password `shelfclock` and open
`http://10.10.10.10`. It never joins a router, so it has no NTP, internet
weather, ESPHome, or Home Assistant connection; local controls, schedules,
songs, the DS3231, and browser-based OTA remain available.

Isolation keeps local wall time in both clocks. Use **Manually Set Date/Time**
to copy the browser's local time to the ESP32 and DS3231. Timezone and DST
settings are retained for network builds; they do not shift Isolation time.
Daily maintenance leaves the DS3231 running independently and does not start NTP.

In weather-enabled network builds, the rain forecast shelf-light mode uses
blue for rain, white for snow, and purple for days with both. Brightness still
represents precipitation probability: below 50% is off, and 50-100% increases
brightness. Unknown precipitation types use blue. These colors are the default
and require no additional setting.

Until a valid network or RTC time becomes available, the clock uses a temporary
local 12:00 display. Changing display mode wakes a display suspended by its
silence timer and restarts that timer. When the saved network name or assigned
IP changes between boots, ShelfClock selects Scrolling mode, enables the IP
address scroll item, and saves that mode so the new address remains visible.

Scheduler values such as `99` and `9999` are deliberate internal
any/irrelevant sentinels. They are generated by the scheduler page according
to schedule type and should not be treated as corrupt times.

## Home Assistant
<img width="1055" height="1031" alt="esphome-HA" src="https://github.com/user-attachments/assets/ca292eb2-d30a-46d3-9c52-fb47de2d532f" />

The ESPHome build exposes the display mode, timer duration and start actions,
scoreboard values, lightshows, spectrum modes, four preset save/load pairs,
date/time synchronization, and network recovery through the native ESPHome
API. If Home Assistant does not discover the clock automatically, add the
ESPHome integration manually with the clock's IP address or hostname.

All controls belong to one ShelfClock device, but Home Assistant decides how
entities appear on dashboards. To show them together, create an Entities card
and add the ShelfClock entities to it. Firmware cannot force Home Assistant to
place every entity in one dashboard card.

See [ESPHome Builds](esphome/README.md) for YAML customization, provisioning,
ownership boundaries, and advanced builds.

## Building From Source

Set the documented `build_flags` in `platformio.ini`, then choose the
firmware host:

| Command | Result |
| --- | --- |
| `Build-ShelfClock-Standalone.cmd` | Standalone Full and OTA images |
| `Build-ShelfClock-ESPHome.cmd` | ESPHome Full and OTA images |

Each command reads the single environment selected by `default_envs`, prints
the effective feature choices, and writes its two files under `Releases`.
`SHELFCLOCK_ISOLATION=true` is accepted only by the Standalone build.
`Upload-Full-Firmware-via-USB.cmd` can install the existing Standalone Full
image after checking for available COM ports and confirming the destructive
operation. Its numbered menu contains only ports Windows currently exposes to
serial applications; remembered but disconnected Device Manager entries are
not selectable.

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

<img width="2252" height="4000" alt="20260927_085031" src="https://github.com/user-attachments/assets/22e86b37-289c-46b2-a13a-386c28c5346d" />

The reference clock uses an ESP32 DevKit, WS2812-compatible LEDs, DS3231 RTC,
DHT11, LDR, INMP441 microphone, and a transistor-driven buzzer. Most peripheral
features can be disabled at compile time. The full clock wiring map defaults to
seven LEDs per segment; the smaller development configuration uses four.

The `3D Printed Parts` and `diagrams` directories contain the existing
mechanical and wiring material. `ShelfClockv2-gerbers.zip` contains the current
reference PCB fabrication files.

<img width="4000" height="1800" alt="20230220_145415" src="https://github.com/user-attachments/assets/463e7c34-6739-4086-918b-a22aceccf162" />

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
