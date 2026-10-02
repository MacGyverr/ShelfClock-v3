# Installing ShelfClock

This guide covers prebuilt release images. Developers compiling the source
should also read [BUILDING.md](../BUILDING.md).

## Select The Correct Build

Release filenames do not expose the developer's Full or Test build environment;
both identify themselves as ShelfClock. Obtain the package intended for the
physical clock. The wiring map matters in addition to the LED count, and using
the wrong build can make the LEDs appear scrambled even when the firmware is
otherwise running correctly. Source builds support one through ten LEDs per
segment.

## Select The Correct Image

| Image | Install method | Data result |
| --- | --- | --- |
| `*-Full-Firmware.bin` | USB at flash offset `0x0` | Complete standalone installation; replaces the whole 4 MB flash |
| `*-OTA-Update.bin` | ShelfClock Update Firmware page | Updates standalone firmware; preserves user data |
| `*-Isolation-Full-Firmware.bin` | USB at flash offset `0x0` | Complete isolated standalone installation; replaces the whole 4 MB flash |
| `*-Isolation-OTA-Update.bin` | ShelfClock Update Firmware page | Updates isolated standalone firmware; preserves user data |
| `*-ESPHome-Full-Firmware.bin` | USB at flash offset `0x0` | Complete ESPHome installation; replaces the whole 4 MB flash |
| `*-OTA-ESPHome.bin` | ESPHome OTA or ShelfClock Update Firmware page | Updates ESPHome firmware; preserves user data |

Both complete images contain the boot components, partition table, application,
and LittleFS content needed by the local interface. Do not upload a complete
image through an OTA page.

## Complete USB Installation

ESPHome Web or another ESP Web Tools installer can write a complete image. A
command-line installation can use `esptool`:

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

The normal Standalone and ESPHome builds show their assigned IP address after
connecting. The Isolation build always uses `10.10.10.10`. Open the applicable
address to use the complete ShelfClock interface.

## Updating Wirelessly

Open **Network & Recovery**, then **Update Firmware**, in the ShelfClock web
interface. Upload only the OTA file matching the currently installed host:

- Standalone: `*-OTA-Update.bin`
- Standalone Isolation: `*-Isolation-OTA-Update.bin`
- ESPHome: `*-OTA-ESPHome.bin`

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
