# Installing ShelfClock

This guide covers prebuilt release images. Developers compiling the source
should also read [BUILDING.md](../BUILDING.md).

## Select The Correct Build

Release filenames do not encode the selected build flags. Obtain the package
compiled for the physical clock. The wiring map matters in addition to the LED
count, and using the wrong build can make the LEDs appear scrambled even when
the firmware is otherwise running correctly. Source builds support one through
ten LEDs per segment.

## Select The Correct Image

| Image | Install method | Data result |
| --- | --- | --- |
| `ShelfClock-Full-Firmware.bin` | USB at flash offset `0x0` | Complete Standalone installation; replaces the selected flash layout |
| `ShelfClock-OTA-Update.bin` | ShelfClock Update Firmware page | Updates Standalone firmware; preserves user data |
| `ShelfClock-ESPHome-Full-Firmware.bin` | USB at flash offset `0x0` | Complete ESPHome installation; replaces the selected flash layout |
| `ShelfClock-ESPHome-OTA-Update.bin` | ESPHome OTA or ShelfClock Update Firmware page | Updates ESPHome firmware; preserves user data |

Both complete images contain the boot components, partition table, application,
and LittleFS content needed by the local interface. Do not upload a complete
image through an OTA page.

Version 3.0.2 adds precipitation colors to the rain forecast shelf-light mode:
blue for rain, white for snow, and purple for days with both. Brightness still
represents precipitation probability; unidentified precipitation stays blue.
This requires weather enabled and network access, not an Isolation build.
Existing installations can receive this change with the matching OTA image;
no filesystem update or new setting is required.

## Complete USB Installation

The hosted `esphome/installer.html` page offers both complete images through
ESP Web Tools. It must be served through HTTPS or localhost; opening it directly
from disk cannot fetch its manifests. The manifests download binaries attached
to the latest GitHub release.

Windows users can flash the existing Standalone complete image with
`Upload-Full-Firmware-via-USB.cmd`. It displays the result of
`Get-PnpDevice -Class Ports`, then offers only currently active serial ports as
numbered choices. Enter the bracketed menu number, not the COM number or name.
An `Unknown` device with `Present=False` is only a remembered Windows entry and
cannot be opened by esptool. An active port can also be supplied with
`-Port COM8`. The uploader requires typing `ERASE` before writing.

A command-line installation can also use `esptool`:

```powershell
python -m esptool --chip esp32 --port COM8 write-flash 0x0 .\Releases\ShelfClock-Full-Firmware.bin
```

Replace `COM8` and the filename as needed. Close serial monitors and other
programs using the port before flashing. Power-cycle the clock if the serial
adapter does not reset it automatically.

## First Wi-Fi Setup

### Standalone

Join the `esp32ap` network. Android normally offers a captive-portal sign-in or
**Manage router** action. If it does not, browse to `http://172.217.28.1` while
still connected to the setup network and select the local Wi-Fi network.

### ESPHome

Provision with ESPHome Web's **Configure Wi-Fi** action or join the
`ShelfClock Setup` fallback access point. The fallback captive portal starts
when the clock cannot connect to saved Wi-Fi.

### Standalone Isolation

Join the permanent `ShelfClock-XXXXXX` access point using password
`shelfclock`, then open `http://10.10.10.10`. The suffix is derived from the
device, so nearby ShelfClocks have distinct names. This build does not join a
router and therefore has no internet weather, NTP, ESPHome, or Home Assistant
connection. It uses its DS3231 or manual time setting and remains directly
reachable from a phone or computer connected to its access point.

Isolation stores local wall time in the ESP32 and DS3231. Use **Manually Set
Date/Time** on the Settings page to set both from the browser's local clock.
The saved timezone and DST controls apply to network builds and do not shift
Isolation time. Daily maintenance does not reconfigure NTP or overwrite the
DS3231.

Version `3.0.1` fixes an Isolation timezone change at the daily rollover. An
existing Isolation installation needs only its matching OTA image, followed by
one manual date/time update to correct any time already shifted by the old
firmware. A filesystem upload is not required for this fix.

The normal Standalone and ESPHome builds show their assigned IP address after
connecting. The Isolation build always uses `10.10.10.10`. Open the applicable
address to use the complete ShelfClock interface.

## Updating Wirelessly

Open **Network & Recovery**, then **Update Firmware**, in the ShelfClock web
interface. Upload only the OTA file matching the currently installed host:

- Standalone, including Isolation: `ShelfClock-OTA-Update.bin`
- ESPHome: `ShelfClock-ESPHome-OTA-Update.bin`

ESPHome firmware can also be updated through ESPHome Device Builder. OTA
updates replace only the application partition and preserve LittleFS, clock
settings, schedules, songs, and Wi-Fi credentials.

A full image is required to switch between standalone and ESPHome because the
two hosts store network and boot information differently.

## PlatformIO Uploads

The ordinary PlatformIO **Upload** task writes only
`.pio\build\<environment>\firmware.bin`. It does not include the filesystem.
For a new standalone installation made directly from PlatformIO, run both:

1. **Upload**
2. **Upload Filesystem Image**

Alternatively, build and flash `*-Full-Firmware.bin` once at offset `0x0`.

## Home Assistant

The ESPHome firmware exposes a native API. Home Assistant may discover it
automatically; otherwise choose **Settings > Devices & services > Add
Integration > ESPHome** and enter the clock's IP address or hostname.

Home Assistant associates all entities with one ShelfClock device but does not
automatically arrange them into one dashboard card. Create an Entities card and
select the desired ShelfClock controls to produce a single grouped panel.

The **Update Date and Time** button appears in the device's Configuration section.
It updates ShelfClock from ESPHome's current valid SNTP/local time. A press made
before ESPHome has valid time is ignored and reported in the ESPHome log. The
ESPHome target does not write the physical DS3231; that RTC remains owned by the
Standalone firmware.

## Wi-Fi Recovery

The ShelfClock **Network & Recovery** page can erase only saved Wi-Fi
credentials and restart into setup mode. It preserves settings, schedules,
songs, and other LittleFS data.

The Isolation build has no router credentials or setup mode. Its recovery
action simply restarts the permanent access point.

If the application no longer boots, install the appropriate complete image by
USB. A complete image is destructive to stored user data.

## Filesystem Compatibility

Standalone and ESPHome releases use the same partition table and LittleFS
content. Application-only updates leave that partition untouched. When a
release changes required web assets or defaults, use a complete image or a
separate PlatformIO filesystem upload as directed by that release's notes.
