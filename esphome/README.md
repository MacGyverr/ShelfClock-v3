# ShelfClock ESPHome Builds

ShelfClock's ESPHome firmware runs the same clock implementation as the
standalone PlatformIO firmware and adds ESPHome's native API, OTA system,
provisioning, time source, and Home Assistant entities.

The permanent-access-point Isolation build is Stand-alone only. It intentionally
has no ESPHome configuration because its purpose is operation without a router
or Home Assistant connection.

## Usable Configurations

| YAML | Wiring map | Default LEDs per segment | Device name |
| --- | --- | --- | --- |
| `shelfclock-test.yaml` | Test environment | 4 | `shelfclock` |
| `shelfclock-full.yaml` | Full clock | 7 | `shelfclock` |

`shelfclock-full.yaml` supplies the full-clock geometry and mapping
substitutions and includes the shared configuration.

`core-probe.yaml` and `dependency-probe.yaml` are compile-only engineering
checks. Do not upload them to a clock.

## Recommended Installation

Set the shared flags in `platformio.ini`, then run
`Build-ShelfClock-ESPHome.cmd` from the repository root. The command reads
the active `default_envs` hardware layout and creates the ESPHome Full and OTA
files. It refuses `SHELFCLOCK_ISOLATION=true`. Install
`ShelfClock-ESPHome-Full-Firmware.bin` by USB for a fresh device. It contains
ESPHome's factory image and the complete ShelfClock LittleFS partition in one
image sized for the active hardware environment.

For later updates, use `ShelfClock-ESPHome-OTA-Update.bin` through ESPHome
OTA or ShelfClock's Update Firmware page. OTA does not replace the filesystem, settings,
schedules, songs, or Wi-Fi credentials. Never upload a complete image through
an OTA page.

`installer.html` offers both Standalone and ESPHome complete installations.
It uses ESP Web Tools and must be hosted through HTTPS or localhost. Its
manifests retrieve the corresponding complete image from the latest GitHub
release.

See [`docs/INSTALLING.md`](../docs/INSTALLING.md) for complete instructions.

## Provisioning And Recovery

No Wi-Fi credentials are stored in the repository or release image. Provision
over USB with an Improv-compatible client such as ESPHome Web, or connect to
the `ShelfClock Setup` fallback access point and use its captive portal.

ShelfClock owns port 80 while station Wi-Fi is connected. It releases that
port while disconnected so ESPHome's captive portal can provide recovery.
The Network & Recovery action erases only Wi-Fi credentials, preserves clock
data, and restarts into setup mode.

## Home Assistant

The native API exposes:

- Display mode selection.
- Timer duration, countdown, and stopwatch actions.
- Left and right scoreboard values.
- Lightshow and spectrum selection.
- Four preset save buttons and four preset load buttons.
- Date/time synchronization from ESPHome's current valid local time.
- Network recovery.
- Indoor temperature and humidity sensors.

If discovery does not offer the clock automatically, add the ESPHome
integration manually using its IP address or hostname. All entities are
associated with one device. Dashboard card layout remains a Home Assistant
configuration choice; create one Entities card to group the desired controls.

The local ShelfClock web interface remains available and uses the same command
path as Home Assistant. The clock continues operating when Home Assistant is
unavailable.

## Ownership Boundaries

In this target, ESPHome owns Wi-Fi, mDNS, fallback provisioning, native API,
SNTP, DHT acquisition, and ESPHome OTA. ShelfClock owns LEDs, the microphone,
buzzer and RTTTL playback, scheduler, timers, settings, songs, presets,
LittleFS, and the local web interface.

ESPHome time remains authoritative in this host; the physical DS3231 driver
belongs to the Standalone adapter. ESPHome's DHT component supplies environment
readings through the host adapter rather than loading a second DHT driver. When
`HAS_DHT=false`, the release script selects an inert sensor package and does
not configure the DHT GPIO.

The `Update Date and Time` configuration button applies ESPHome's current valid
SNTP/local time through the same shared time-setting logic used by ShelfClock's
web interface. If ESPHome has not obtained valid time yet, the press is ignored
and a warning is written to the ESPHome log. Because ESPHome owns time in this
target, this action does not write the physical DS3231.

The ESPHome target uses FastLED's clockless SPI transport because the IDF 5 RMT
transport produced corrupted LED frames under Wi-Fi interrupt load on the
development hardware. The standalone target retains its established FastLED
transport.

## Custom Builds

The release command translates `TEST_CLOCK`, optional
`LEDS_PER_SEGMENT`, and all `HAS_*` values from PlatformIO's effective
`build_flags` into ESPHome substitutions. `LEDS_PER_SEGMENT` accepts values
from 1 through 10. `TEST_CLOCK` selects a physical shelf-light mapping, not
merely a size.

The ESPHome build command initializes the pinned local environment
automatically. After it has run, advanced compile checks can use:

```powershell
& .\.tools\esphome\Scripts\python.exe tools\run_esphome_probe.py --target runtime --variant test
& .\.tools\esphome\Scripts\python.exe tools\run_esphome_probe.py --target runtime --variant full
```

The project currently pins Python 3.12-compatible ESPHome dependencies,
ESPHome 2026.9.0, Arduino 3.3.11, and the libraries declared in the YAML. The
runner isolates generated output under `.cache\esphome` and does not upload.

ESPHome Device Builder can adopt the GitHub package after the repository and
dashboard import URL are public. Package mode is required because the YAML
depends on sibling source, headers, libraries, and the local external
component; copying only the YAML file is insufficient.

## Shared Source

The YAML stages `src/ShelfClock.cpp`, project headers, and
`lib/ShelfClockCore/src/shelfclock` through ESPHome's supported build
mechanisms. Do not edit generated copies under `.cache`; they are disposable.
The ESPHome build is a separately linked host application and does not wrap or
reuse PlatformIO's `firmware.bin`.
