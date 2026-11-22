# ULS-03GW clean firmware

[![tests](https://github.com/NzLHD/ULS-03GW/actions/workflows/ci.yml/badge.svg)](https://github.com/NzLHD/ULS-03GW/actions/workflows/ci.yml)

Cloud-free firmware for the **ULS-03GW ultrasonic tank level sensor** (Tuya T1-U-HL module, Beken BK7238). It
reports the tank level to **your own MQTT broker** (TLS or plain), with **Home Assistant discovery**. No Tuya
cloud, no Tuya code.

- **Battery:** reads every 30 min with Wi-Fi off and sends on change, alarm, button press or a 6 h heartbeat.
  Sleep is 52 µA, which gives an estimated ~5 years on 3× 18650.
- **USB power:** stays connected and reads every 10 s.
- **Setup:** a Wi-Fi access point with a web page (captive portal) to set everything.

> Tested on one device (HW V1.3). Flashing replaces the Tuya app; keep your backup to go back.

## Is this your device?

| | |
|---|---|
| Sold as | **ULS-03GW**, also **CSB-03GW** (HojellyTek), a Wi-Fi ultrasonic tank/liquid level sensor for the Tuya / Smart Life app |
| Board marking | `T1-U_liquid_sensor HW: V1.3` |
| Wi-Fi module | Tuya **T1-U-HL** (Beken **BK7238**, 2 MB flash) |
| Stock firmware | Tuya "T1-U_liquid_level_sensor" 1.2.22 |
| Sensor head | separate 4-pin serial ultrasonic head (`+ G TX RX`) |

Other listings with the same board and module will probably work, but are untested. Please report them with
the *Hardware report* issue template.

The firmware was built by Claude (Anthropic's AI model). The developer tested and verified it on hardware.

## 1. What you need

- A USB-UART adapter at **3.3 V logic** (FT232/CH340/CP2102).
- A separate **3.3 V supply**: the adapter's 3V3 pin is too weak.
- [BK7231GUIFlashTool](https://github.com/openshwprojects/BK7231GUIFlashTool), run in **BK7238** mode.
- The image `uls03gw_UA_0.2.2.bin` from the [latest release](https://github.com/NzLHD/ULS-03GW/releases/latest).
  Check it against the release's `SHA256SUMS`.

## 2. Wiring (T1-U-HL module pads)

| Purpose | Adapter / supply | Module pad |
|---|---|---|
| Flash: data out | adapter **RX** | **15** (TX1, P11) |
| Flash: data in | adapter **TX** | **16** (RX1, P10) |
| Ground | adapter GND + supply GND | **13** (GND) |
| Power | 3.3 V supply | **14** (3V3) |
| Reset into the bootloader | tap to GND when the flasher waits | **18** (CEN) |
| Log only (optional) | adapter **RX**, 115200 8N1 | **7** (TX2, P0) |

**Switch SW2 off and unplug the sensor head** before flashing: the head uses pads 15/16 too. Full pinout:
[docs/hardware.md](docs/hardware.md).

## 3. Flash

1. **Back up first:** read the full 2 MB twice and compare the checksums. The dump holds your device's keys,
   so keep it private.
2. Write `uls03gw_UA_0.2.2.bin` at **0x11000**.
   - **Never write a "QIO"/full image**: it replaces the bootloader.
   - The firmware leaves the bootloader, the factory RF calibration and the MAC alone.
3. Optional: erase 0x1F5000–0x200000 (the old Tuya data). The firmware ignores it either way.
4. Disconnect the adapter (it back-powers the board), reconnect the head, power up.

To go back to stock, write your backup.

## 4. Set up

1. Join Wi-Fi **`ULS-03GW-XXXXXX`**, password **`uls03gw-setup`**. The page opens by itself; if not, go to
   http://192.168.4.1/.
2. Fill in Wi-Fi, MQTT and the tank:
   - For TLS, paste your broker's **CA certificate** (not its key).
   - Tank: the install height (sensor to bottom) and the full distance (sensor to the full line).
3. Press **Test Wi-Fi + MQTT**, then **Save**. Press the button (or wait 5 min) to leave setup.

Change the AP password on first setup (Device section).

## 5. Use

| Button | Action |
|---|---|
| short press | read and send now (blue flash = sent, red ×3 = failed) |
| hold 4 s (blue solid) | setup access point again |
| hold 15 s (blue + red blink) | factory reset |

MQTT, with `<base>` = `uls03gw/xxxxxx` by default:

- `<base>/state`: JSON with `distance_mm`, `depth_cm`, `level_pct`, `alarm`, `volume_l`, `rssi`… (retained)
- `<base>/set`: publish **retained** JSON to change tank/schedule/sensor settings. A sleeping device picks it
  up at its next send. `{"cmd":"report"}` asks for a full report.
- `<base>/status`: `online`/`offline` (USB mode only).
- Home Assistant finds the device automatically: level, depth, volume, alarm, signal, plus editable numbers.

Debug log: pad 7 at 115200. Passwords and keys are never printed.

## Build from source

Linux + Docker:

```bash
git clone --depth 1 https://github.com/NonPIayerCharacter/beken_freertos_sdk.git sdk/beken_freertos_sdk
firmware/tests/run.sh                     # unit tests (PC)
firmware/scripts/build.sh uls03gw         # -> firmware/out/uls03gw/bk7238_UA.bin
```

User manual (settings, tank measurements, Home Assistant): [docs/ULS-03GW_manual.pdf](docs/ULS-03GW_manual.pdf).
Design and protocol details: [docs/spec.md](docs/spec.md). Code notes: [firmware/README.md](firmware/README.md).
Changes: [CHANGELOG.md](CHANGELOG.md). Contributing: [CONTRIBUTING.md](CONTRIBUTING.md). Security reports:
[SECURITY.md](SECURITY.md).

## Related projects

- [OpenBeken](https://github.com/openshwprojects/OpenBK7231T_App): general firmware for Beken and other Tuya
  modules, including BK7238. It does not know this sensor's head protocol or its battery/deep-sleep power
  modes, which is why this firmware exists.
- [BK7231GUIFlashTool](https://github.com/openshwprojects/BK7231GUIFlashTool): backup and flashing.
- [beken_freertos_sdk](https://github.com/NonPIayerCharacter/beken_freertos_sdk): the SDK this firmware builds on.

## License

[MIT](LICENSE) for this repository's source code and documents. The prebuilt image also contains the Beken
SDK, including its closed Wi-Fi/RF libraries, which are not covered by the MIT license.
