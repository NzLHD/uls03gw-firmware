# ULS-03GW clean firmware: build specification

A cloud-free replacement for the stock Tuya firmware on the ULS-03GW ultrasonic tank level sensor
(Tuya T1-U-HL module, Beken BK7238). It reads the tank level, pushes it to **your own MQTT broker
over TLS**, and opens a **setup access point with a web page** when the button is held, as the
stock firmware does for Tuya pairing.

Decisions already made:

| Topic | Choice |
|---|---|
| Firmware base | **Lean custom firmware** on the Beken BK7238 SDK (FreeRTOS + lwIP + mbedTLS). No Tuya code. |
| TLS trust | **CA certificate pasted on the setup page.** The device verifies the broker against it. Plain MQTT is still allowed as an option. |
| MQTT format | **One JSON state message per send + Home Assistant MQTT discovery** |
| Battery schedule | **Read every 30 min with Wi-Fi off. Send on change, alarm, button press, or a 6 h heartbeat.** |

Related files:

- [hardware.md](hardware.md): pinout, power measurements, sensor protocol, flash layout
- [../README.md](../README.md): wiring, flashing and setup for users; [../firmware/README.md](../firmware/README.md)
  for developers
- "Stock" values below come from an analysis of the Tuya firmware, which is not published (§0, Background).

Items marked **⚠ verify** have not been confirmed on hardware or in an SDK yet. The SDK is chosen in §11.

---

## 0. Status (firmware 0.2.3)

### Confirmed on hardware (one device, HW V1.3)

- **Both power modes:** USB mode (persistent MQTT session, online/offline last will) and battery mode (read,
  decide, send, deep sleep). Switching USB → battery reboots into battery mode. Settings survive a power cycle.
- **Setup AP:** the page opens by itself (captive DNS, patch 0004), `/api/test` works with the AP up.
- **TLS:** TLSv1.2 `ECDHE-ECDSA-AES256-GCM-SHA384` against an own ECDSA CA. Handshake 2.1 s, Wi-Fi association
  + DHCP 1.2–3.0 s, SNTP ~0.1 s, free heap at boot 151 KB. Certificate dates are checked against SNTP time.
- **No secrets in the log:** the closed `libsupplicant` prints the passphrase and session keys; a `bk_printf`
  wrapper (`glue/log_filter.c`) hides them.
- **Home Assistant discovery**, LED patterns, the 45 s awake cap.
- **Flash layout:** the RF/MAC partition is moved to Tuya's 0x1E3000 (patch 0002). The factory MAC and the TX
  power / XTAL calibration load from it, and the image cannot overwrite it (§11).
- **Deep sleep:** RTC wake is exact and the P6 button wakes it. Tuya's bootloader doesn't keep the GPIO wake
  status, so patch 0003 reads the live wake register; button wakes and power-on are told apart correctly.
  Wake → `user_main` takes ~0.26 s.
- **Sleep current 52.2 µA** for the whole board at 4.2 V (stock: 51.6 µA, within meter resolution). P9 is left
  to the SDK's high-Z sweep in sleep; keeping its pull-up made no difference (52.4 µA).
- **Sensor head** (§2): replies in ~25 ms; requests must be ≥ ~450–500 ms apart, and each reply carries the
  previous measurement. A reading takes **~1 s instead of stock's ~4.5 s**.

### Power budget

- Stock power path: "14VF" 5-pin buck → module 3.3 V. AP mode 56 mA, awake 26 mA, deep sleep ~52 µA.
- Battery mode uses ≈ 0.72–0.8 Ah/yr with the ~1 s / 16 mA reading (§2, §5.1). Readings, not sends, dominate;
  the send cost (~0.08 mAh) is still an estimate.
- Measuring tip: bypass the µA meter while booting (or use a 100 Ω shunt + 1N4148 bypass), and disconnect the
  USB-UART adapter, which back-powers the board through TX/RX.
- **Solar (1× 18650):** 6 V 1 W panel (0.5 W minimum) and a CN3065 / CN3791 / TP4056+DW01 charger into the
  **battery connector**, never USB (USB pulls P9 LOW, so the device never sleeps). Add a 10 kΩ NTC so it
  doesn't charge below 0 °C.

### Fixed in 0.2.3 (confirmed on hardware, 2026-11-22)

- **USB mode no longer freezes** when the broker restarts or Wi-Fi drops. A TLS write to a dead connection used
  to retry forever. Writes now give up after 5 s, a broker that stops answering pings for 1.5 × keepalive or
  a missing PUBACK closes the session, and USB mode reconnects and resends. Confirmed working on USB by the owner.
- USB mode and the setup page run under a software watchdog (180 s); the SDK's own watchdogs never catch a
  stuck application thread.
- The battery awake cap no longer writes flash while the stuck thread might be writing it.
- **Battery mode wake detection checked** (5 min reads, 1 h heartbeat): timer wakes report `start_type` 4
  (DEEPPS_RTC; `SCTRL_ROSC_TIMER` bit 8 set), button wakes 3 (DEEPPS_GPIO, `gpio_raw` 0x40), retention 2 on
  both. The clock advances per wake, and an unchanged level with the heartbeat not yet due correctly sends
  nothing. A reported "no second message" was the send rules: change > 2 cm, alarm, button, or heartbeat.

### Known issues

- A button wake doesn't advance the clock (the part of the sleep before the press is lost), so each press
  delays the heartbeat by up to one read interval.
- The SDK's supplicant prints the Wi-Fi network name as hex (`new ie: 0 : …`) in the debug log; remove it
  before sharing a log.
- Only one device and hardware revision (V1.3) has been tested.

### Roadmap

- Fast Wi-Fi reconnect: cached BSSID/channel/PMK in its own A/B record (§8).
- TLS session tickets; a LAN web UI in USB mode.
- OTA: the format of Tuya's "diff2ya" bootloader (§11).
- Battery voltage: an optional divider on ADC P20 (§12).
- Confirm sensor byte 10 = °C at another temperature; measure the head's current during a reading.

### Background

The stock firmware (Tuya "T1-U_liquid_level_sensor" v1.2.22, TuyaOS 3.11.16) was analysed to recover the GPIO
map, the sensor protocol, the data points and the level maths. That analysis is not published: it is derived
from proprietary code. In short, the stock app has no backdoor, but it does have Tuya cloud OTA/reset/unbind, a
remote debug log that prints the Wi-Fi password, a factory test mode on the UART, no flash encryption and
China-region endpoints.

A flash dump of a device holds its Tuya keys and Wi-Fi credentials. Keep your backup private.

---

## 1. Hardware facts the firmware depends on

All pins below are Beken GPIO numbers (Pxx), recovered from the stock binary.

| Function | Pin / pad | Direction | Notes |
|---|---|---|---|
| Button | **P6** / pad 11 | input, internal pull-up | Active LOW. **Also the deep-sleep wake source** (wake on LOW). |
| Power-mode detect | **P9** / pad 9 | input, internal pull-up | **HIGH = battery**, LOW = USB/mains. Read at every boot. |
| Sensor head power | **P26** / pad 10 | push-pull output | HIGH = head powered. Drive LOW before sleeping. |
| Sensor UART | **UART1: P10 RX / P11 TX** (pads 16/15) | 9600 8N1 | The SDK may call this "UART0" (Tuya numbering) or "UART1" (Beken). **Same pads as flashing.** |
| Blue LED (network) | **P21** / pad 19 | output, active HIGH | |
| Red LED (power) | **P17** / pad 20 | output, active HIGH | Stock sets it HIGH at boot and leaves it. |
| Debug log | **UART2 TX: P0** / pad 7 | 115200 8N1 | Free to use for logs. Never log secrets. |
| Battery voltage | none | | The stock firmware has no ADC reading. See §12. |

Power:

- Battery (3× 18650 in parallel, or 1× 18650 + solar) → SW2 → "14VF" buck regulator → module 3.3 V (pad 14).
- Measured at 4.2 V on the battery connector, stock firmware:

  | State | Current |
  |---|---|
  | Wi-Fi AP mode | 56 mA |
  | Awake, radio on | 26 mA |
  | Deep sleep, whole board | **51.6 µA** |

  51.6 µA is the floor the new firmware must match or beat.
- A USB-TTL adapter left on pads 15/16 back-powers the board. Remove it in normal use.

## 2. Sensor head protocol (reuse `uls_proto.c`)

- Host polls; the head never transmits on its own.
- Request, 8 bytes: `18 08 6A FF FF 27 CE 0D`.
  - Header `0x18`, length `0x08`, command `0x6A`, data `FF FF`.
  - CRC-16/MODBUS (poly 0xA001 reflected, init 0xFFFF) over the first 5 bytes, low byte first.
  - Tail `0x0D`.
- Response, 14 bytes: `18 0E 6A ?? ?? DL DH ?? ?? ?? ?? CL CH 0D`.
  - `distance_mm = DH << 8 | DL`. **0 means no valid echo.**
- Measured on hardware (with a test app since removed, 2026-11-22), every reply:
  `18 0E 6A FF FF DL DH FF FF FF TT 3F AC 0D`
  - Bytes 3–4 = `FF FF`, an echo of the request's data. Bytes 7–9 = `FF`.
  - **Bytes 11–12 are not a CRC:** always `3F AC`, even when the distance and byte 10 change. Keep
    `check_crc = 0`, as stock does. Validate by header/length/command/tail only.
  - Byte 10 (`TT`) = **temperature in °C, very likely**: 0x1C = 28 in a 28–29 °C room (0x1D early on). Usable for
    a `temp_c` diagnostic and for speed-of-sound compensation if the head doesn't do it itself. **⚠ check**
    once at a clearly different temperature.
- Timing (all measured):
  - The head answers every request in ~20–28 ms.
  - **At power-on it sends 5 junk bytes unprompted.** Flush RX before the first request.
  - **Each request starts a measurement, and the reply carries the previous result.** The head doesn't measure
    on its own: a first request at 750 ms still returns 0.
  - **Requests closer than ~450–500 ms apart never yield a distance.** At 150/200/250/300/400 ms it's 0
    throughout; at 500 ms it works.
  - **A request sent less than ~250–500 ms after P26 HIGH starts an invalid measurement.** A trigger at
    500 ms gives a valid reply at 1000 ms; a trigger at 250 ms doesn't (valid only at 1250 ms).
  - The value only changes every ~2.5 s, so the head filters internally. Repeated reads within that window
    return the same number.
  - **⚠ Open:** in 2 of 4 runs the head stopped answering after its first valid reply (timeouts on the next
    requests). The firmware takes one reading, so retries and the give-up limit cover it. Watch for it.

Reading procedure (one "reading"; based on the measurements above, stock in brackets):

1. P26 HIGH. Wait `sensor_warmup_ms` (default **500**) [stock 2000], then flush RX (the 5 power-up bytes).
2. Send a **trigger request** and ignore its reply (it carries no measurement).
3. Every `sensor_poll_ms` (default 500, minimum 450) send another request. Collect `sensor_samples`
   (default **1**) [stock 5] non-zero distances.
   - The head filters internally and updates only every ~2.5 s, so extra samples mostly repeat the same
     value.
   - With 3 or more samples, use the trimmed mean (`uls_trimmed_avg`, drop min and max). With 2, use the
     rounded mean; with 1, the value itself.
4. Give up after `sensor_max_polls` (default 6, i.e. 3 s) without enough samples. Report `sensor_ok: false`.
   A reply of 0 = no echo; a missing reply = timeout. Both count as polls.
5. P26 LOW. Set the UART TX pin to input (or output LOW) so it can't back-power the unpowered head.

With the defaults, one reading is ~1.0 s of head-on time after P26 HIGH (stock ~4.5 s).

## 3. Level maths (from stock `dp_process.c` / `app_sensor.c`, with the stock bugs fixed)

```
depth_cm  = install_height_cm - round(distance_mm / 10)
span_cm   = install_height_cm - full_distance_cm
configured = span_cm > 0
level_pct = configured ? clamp(depth_cm * 100 / span_cm, 0, 100) : null
alarm     = "upper" if upper_alarm_pct > 0 and level_pct >= upper_alarm_pct
            "lower" if lower_alarm_pct > 0 and level_pct <  lower_alarm_pct
            "normal" otherwise
volume_l  = tank_volume_l > 0 and configured ? round(level_pct * tank_volume_l / 100) : null
```

Differences from stock, and why:

| Stock behaviour | New behaviour |
|---|---|
| Divides by zero when height == full distance (the stock default) | `level_pct = null`, `configured = false` |
| A negative percent is not clamped | Clamped to 0..100 |
| Negative depth: silently skips the report | Report `depth_cm` clamped to 0 and `err: "distance_gt_height"` |
| Upper alarm 0 (stock default) makes every reading an alarm | 0 = alarm disabled |
| Stock truncates mm/10 | Round to nearest cm |
| Unchanged-depth path reports the previous depth | Always report the latest value |

`volume_l` is new: an optional tank volume, 0 = off.

## 4. Settings and defaults

Defaults follow stock where stock has a value. Every setting can be changed on the setup page. The tank,
schedule and sensor settings can also be changed over MQTT (§7.4); Wi-Fi, MQTT, CA and AP settings
cannot, so a bad command can't cut the device off.

| Key | Default | Range | Source / note |
|---|---|---|---|
| `install_height_cm` | 10 | 1–1000 | stock default (DP19) |
| `full_distance_cm` | 10 | 0–999 | stock default (DP21). With height 10 the device reports `configured: false` until set. |
| `upper_alarm_pct` | 0 (off) | 0–100 | stock DP7. 0 now means off. |
| `lower_alarm_pct` | 0 (off) | 0–100 | stock DP8 |
| `tank_volume_l` | 0 (off) | 0–100000 | new |
| `change_threshold_cm` | 2 | 1–50 | stock hysteresis: send when \|Δ\| > 2 cm |
| `read_interval_min` | 30 | 5–1440 | battery mode: how often to wake and read |
| `heartbeat_h` | 6 | 1–24 | battery mode: send even if unchanged. Replaces stock DP102 (default 1 h). |
| `mains_read_s` | 10 | 2–3600 | USB mode: read period |
| `mains_heartbeat_min` | 30 | 5–1440 | USB mode: stock re-reports at :00/:30 |
| `sensor_warmup_ms` | 500 | 100–5000 | before the trigger request. Measured: 500 works, 250 too early (§2). Stock 2000. |
| `sensor_poll_ms` | 500 | 450–2000 | measured: ≤ 400 ms never gives a distance (§2). Stock 500. |
| `sensor_samples` | 1 | 1–15 | the head filters internally (§2). Stock 5. |
| `ap_timeout_min` | 5 | 1–30 | setup AP closes after this long with no client activity |
| `ap_password` | `uls03gw-setup` | 8–63 chars, or empty for open | **change on first setup** |
| `wifi_ssid`, `wifi_pass` | empty | | |
| `wifi_static` | off | | optional `ip`, `mask`, `gw`, `dns`. Saves about 1 s per send. |
| `hostname` | `uls03gw-XXXXXX` | | XXXXXX = last 3 MAC bytes in hex |
| `mqtt_host` | empty | name or IP | |
| `mqtt_port` | 8883 | | 1883 if TLS off |
| `mqtt_tls` | on | | |
| `mqtt_ca_pem` | empty | ≤ 4 KB PEM | required when TLS is on |
| `mqtt_verify_host` | on | | check the broker name against the certificate SAN |
| `mqtt_user`, `mqtt_pass` | empty | | |
| `mqtt_client_id` | `uls03gw-XXXXXX` | | |
| `mqtt_base_topic` | `uls03gw/XXXXXX` | | |
| `ha_discovery` | on | | |
| `ha_prefix` | `homeassistant` | | |

### Storage

- **Config:** one struct, `{magic, version, len, seq, CRC32, data}`, written to two slots alternately (A/B). Each
  slot is **8 KB**: the struct is 4.8 KB with the CA. On boot, load the valid copy with the higher `seq`. Write it
  only when settings change. (`core/abstore.c`)
- **Run state** that must survive deep sleep (BK7238 deep sleep is a reset; assume no RAM is retained,
  **⚠ verify**). Append-only **64-byte** records in a two-sector ring (64 records per sector), CRC per record
  (`core/runstate.c`):
  - policy state: last sent depth / alarm / flags, last send time, failure count and back-off
  - the minute clock across sleeps, boot count, reads since the last send, last distance
  - HA discovery hash + time, last SNTP time
- **Wi-Fi cache** (BSSID + channel + PMK, optional DHCP lease and TLS session ticket) changes rarely. It gets its
  own A/B record, written only when it changes. Not built yet.
- At one record per 30-minute read that is ≈ 17,500 records a year, about 140 erases per sector a year.
  Flash endurance is ~100k cycles, so this is not a concern.
- Location: the old Tuya KV area at **0x1F5000–0x200000** (after you keep the backup).
  - The config struct is 4,800 bytes with a 4 KB CA, so each copy needs **two sectors**.
  - **Config A at 0x1FA000 and B at 0x1FC000** (8 KB each). **Run state at 0x1FE000/0x1FF000.**
  - Checked against the SDK's 2 MB partition table: its nearest areas are BLE bonding at 0x1F0000 (BLE off) and
    the AT-server areas at 0x1EE000/0x1EF000 (not built). Nothing overlaps.
  - Implemented in `firmware/core/abstore.c`. The header is written last, so a power cut keeps the old copy.

## 5. Modes and behaviour

```
                 boot (power-on, deep-sleep wake, or reset)
                                  │
                read P9, load config + run state, read wake reason
                                  │
     ┌──────────── button held ≥ 4 s ─────────────┐
     │                                             ▼
     │                                  SETUP AP MODE (§6)
     ▼
 P9 HIGH: BATTERY MODE (§5.1)         P9 LOW: MAINS MODE (§5.2)
```

Wi-Fi not configured → go straight to setup AP mode (stock also opens pairing when unpaired).

### 5.1 Battery mode (P9 HIGH)

On every wake:

1. **Timer wake:** take a reading (§2). Wi-Fi stays **off**.
2. Decide whether to send. Send if any of these is true:
   - \|depth − last_sent_depth\| > `change_threshold_cm`
   - the alarm state changed
   - `configured` or `sensor_ok` changed
   - time since last send ≥ `heartbeat_h`
   - settings changed on the device
   - **button wake (short press): always send ("report now")**
3. If sending: connect Wi-Fi (fast path, §8), connect MQTT, publish, read retained commands (§7.4), disconnect.
4. Append the run-state record. Drive P26 LOW, LEDs off, UART pins safe. Set wake sources:
   timer = `read_interval_min`, GPIO P6 LOW. Enter deep sleep.

Hard limits so a fault can't drain the battery:

- **Awake cap:** 45 s per wake (reading + send), enforced by a hardware watchdog plus a software timer. When
  it trips, record the failure and sleep.
- **Wi-Fi/MQTT failure back-off:** after each failed send, skip sends for 1, 2, 4… heartbeats, capped at 24 h.
  Keep reading on schedule. An alarm change still tries once per change. Reset the back-off on success.

Expected energy (estimates from §1 measurements, **⚠ confirm** with a shunt and scope):

| Item | Cost |
|---|---|
| Sleep | 51.6 µA → 452 mAh/yr |
| Reading: boot ~0.26 s + head ~1.0 s, **16 mA measured** (whole board at 4.2 V during a reading) | ~0.006 mAh (stock procedure: ~0.04) |
| TLS send (fast Wi-Fi + ECDSA handshake + publish, ~5 s, ~60 mA) | ~0.08 mAh |
| **Default schedule:** 17,520 reads + ~2,000 sends a year | **≈ 0.72 Ah/yr**: sleep 457 + reads ~100 + sends ~160 mAh (stock procedure: 1.35) |

- **3× 18650:** about 5 years (estimate; self-discharge and cold start to matter at this point; stock reading procedure: 2.5–3).
- **1× 18650 alone:** about 2.4 years. With a 1 W solar panel and charger it runs indefinitely.
- Reads still dominate after sleep, so the measured ~1 s reading procedure (§2) is the biggest saving.

### 5.2 Mains mode (P9 LOW, USB powered)

- Stays awake. Wi-Fi connected with modem sleep (DTIM power save) enabled. MQTT session kept open.
- Takes a reading every `mains_read_s`. Head power can stay on.
- Sends using the same rules, with `mains_heartbeat_min` as the heartbeat.
- MQTT last will: `<base>/status` = `offline` (retained). Publish `online` (retained) after connecting.
- Red LED on (as in the manual: "on in normal mode").
- If P9 changes while running (USB pulled or plugged in), reboot into the matching mode.

### 5.3 Button (P6)

| Action | Result |
|---|---|
| Short press (< 1 s) | Battery: wake, read, **send now**. Mains: read and send now. |
| Hold ≥ 4 s (stock: 4 s) | Blue LED goes solid; on release, enter **setup AP mode** |
| Hold ≥ 15 s | Blue + red LEDs blink fast; on release, **factory reset** (wipe config and run state, reboot into AP mode) |
| Any press while in AP mode | Leave AP mode (sleep in battery mode, reconnect in mains mode) |

- In battery mode a press wakes the chip, which reboots. Measure the hold time from boot while P6 stays LOW,
  and add the measured boot time (**⚠ measure**, likely 100–300 ms).
- Debounce 30 ms.

### 5.4 LEDs

| State | Blue P21 | Red P17 |
|---|---|---|
| Setup AP mode | slow blink, 1 Hz | off |
| Hold reached 4 s | solid | off |
| Connecting (only after a button wake) | fast blink | off |
| Send OK (only after a button wake) | 1 flash, 200 ms | off |
| Send failed / sensor error (only after a button wake) | off | 3 flashes |
| Mains mode, connected | off (or solid, configurable) | on |
| Battery mode, timer wake | **off** | **off** |

Timer wakes must not light any LED; it is pure battery drain.

## 6. Setup AP and web page

### Access point

- SSID: `ULS-03GW-XXXXXX` (last 3 MAC bytes).
- WPA2-PSK with `ap_password` (default `uls03gw-setup`). If the password is empty, the AP is open.
  Show a warning on the page and on the label.
- IP `192.168.4.1/24`. DHCP server. DNS server that answers every name with 192.168.4.1, giving a
  captive portal so phones open the page automatically. Answer `/generate_204`, `/hotspot-detect.html` and
  `/connecttest.txt` with a redirect to `/`.
- Closes after `ap_timeout_min` without a request, or on a button press. Then sleeps (battery) or
  connects (mains).
- While AP mode runs, also scan for networks so the page can show a list (AP+STA mode). If the SDK can't scan
  while the AP runs, scan once just before starting the AP.

### Page

- One self-contained HTML page with inline CSS/JS and no external assets (there's no internet on the AP).
  Gzipped in flash, under 20 KB.

| Section | Fields |
|---|---|
| Status | firmware version, MAC, power mode (battery/USB), Wi-Fi RSSI of the last connection, last reading (distance, depth, %, alarm), last send result and error |
| Wi-Fi | SSID (dropdown from scan + free text), password, hostname, static IP toggle with ip/mask/gateway/dns |
| MQTT | host, port, TLS toggle, CA certificate (textarea **and** file upload), verify-hostname toggle, username, password, client ID, base topic, HA discovery toggle + prefix |
| Tank | install height, full distance, tank volume, upper alarm %, lower alarm %. A live reading with a "use current distance as full" helper. |
| Schedule | read interval, heartbeat, mains read period, mains heartbeat, change threshold |
| Sensor (advanced) | warm-up, poll period, samples |
| Device | AP password, AP timeout |
| Actions | **Save**, **Test Wi-Fi + MQTT** (connects as station, publishes a test message, shows the TLS/MQTT error text), **Read sensor now**, **Factory reset** (needs confirmation), **Reboot** |

### HTTP endpoints

| Method | Path | Purpose |
|---|---|---|
| GET | `/` | the page |
| GET | `/api/status` | JSON status + current settings, **secrets omitted** |
| GET | `/api/scan` | JSON list `[{ssid, rssi, auth, ch}]` |
| GET | `/api/read` | take one reading now, return `{distance_mm, depth_cm, level_pct, alarm, sensor_ok, raw}` |
| POST | `/api/config` | JSON body with any subset of settings. Validate everything, then save atomically. |
| POST | `/api/test` | try Wi-Fi + MQTT with the posted or saved settings, return a step-by-step result |
| POST | `/api/reset` | factory reset |
| POST | `/api/reboot` | reboot |

Rules:

- Passwords and the CA are **write-only**. The page shows "(unchanged)", and an empty field keeps the old value.
  A "clear" checkbox removes a value.
- Validate on the device: ranges from §4, the PEM parses (`mbedtls_x509_crt_parse`), and host and port are present
  when MQTT is enabled.
- No web server in battery timer wakes. In mains mode the web UI is **off** in v1 (possible v2 option: LAN UI
  behind a password).

## 7. MQTT

### 7.1 Connection

- MQTT 3.1.1 over TLS 1.2 (mbedTLS). Clean session. Keepalive 60 s.
- Battery mode: connect, publish, collect retained commands, `DISCONNECT`, Wi-Fi off. No last will (a sleeping
  device is not "offline"; Home Assistant uses `expire_after` instead).
- State message: **QoS 1, retained**. Wait for PUBACK before sleeping, so a send counts as successful only once
  the broker has it.

### 7.2 Topics

`<base>` = `mqtt_base_topic`, default `uls03gw/XXXXXX`.

| Topic | Dir | Retained | Content |
|---|---|---|---|
| `<base>/state` | device → broker | yes | JSON state (below) |
| `<base>/config` | device → broker | yes | current settings, secrets omitted (after boot or a change) |
| `<base>/set` | broker → device | **yes** (sender sets retain) | JSON with settings to change |
| `<base>/status` | device → broker | yes | `online` / `offline` (**mains mode only**) |
| `<ha_prefix>/…/config` | device → broker | yes | discovery (§7.5) |

### 7.3 State payload

```json
{
  "distance_mm": 1234,
  "depth_cm": 66,
  "level_pct": 43,
  "alarm": "normal",
  "volume_l": 860,
  "configured": true,
  "sensor_ok": true,
  "err": null,
  "power": "battery",
  "wake": "timer",
  "rssi": -62,
  "reads_since_send": 5,
  "boot_count": 412,
  "fw": "1.0.0"
}
```

- `alarm` is `normal`, `lower` or `upper`.
- `wake` is `timer`, `button`, `power_on` or `watchdog`.
- `err` is null, `sensor_timeout`, `distance_gt_height` or `not_configured`.
- Keys with no value are `null`, never missing.

### 7.4 Commands to a sleeping device

A battery device is reachable only for a few seconds each time it sends, so commands are **retained**:

1. You (or Home Assistant) publish retained JSON to `<base>/set`, for example
   `{"install_height_cm": 180, "heartbeat_h": 3}`.
2. At its next send, the device subscribes to `<base>/set`. It waits for the retained message, or at most
   500 ms after SUBACK.
3. It validates and applies the settings, saves the config, publishes `<base>/config`, then publishes an
   **empty retained message to `<base>/set`** to clear it.
4. Allowed over MQTT: everything in §4 except Wi-Fi, MQTT, CA and AP settings. Those can only be changed
   on the setup page, so a bad command can't strand the device.
5. Special commands: `{"cmd": "report"}` sends a full state + discovery on the next wake, and `{"cmd": "reboot"}`.

Latency: up to one heartbeat in battery mode, or next send. Pressing the button applies pending commands at once.

### 7.5 Home Assistant discovery

- Published retained on the first successful connection, after any settings change, after a `report` command,
  and otherwise **once a day at most**. A hash of the payload is stored in run state, so normal sends stay small.
- One device: `{"identifiers":["uls03gw_XXXXXX"], "name":"Tank level", "model":"ULS-03GW", "manufacturer":"custom", "sw_version":"1.0.0"}`.
- Every entity uses `state_topic: <base>/state` with a `value_template`.
- Battery mode sets `expire_after = 2 × heartbeat_h + 10 min`.

| Component | object_id | Template / unit / class |
|---|---|---|
| sensor | `level` | `{{ value_json.level_pct }}`, `%` |
| sensor | `depth` | `{{ value_json.depth_cm }}`, `cm`, device_class `distance` |
| sensor | `distance` | `{{ value_json.distance_mm }}`, `mm`, `distance`, diagnostic |
| sensor | `volume` | `{{ value_json.volume_l }}`, `L`, device_class `volume_storage` (only if `tank_volume_l` > 0) |
| sensor | `alarm` | `{{ value_json.alarm }}`, device_class `enum`, options `normal`/`lower`/`upper` |
| binary_sensor | `problem` | `{{ 'ON' if value_json.err else 'OFF' }}`, device_class `problem` |
| sensor | `rssi` | `{{ value_json.rssi }}`, `dBm`, `signal_strength`, diagnostic |
| sensor | `power` | `{{ value_json.power }}`, diagnostic |
| number | `install_height`, `full_distance`, `upper_alarm`, `lower_alarm`, `heartbeat`, `read_interval` | `command_topic: <base>/set`, `retain: true`, `command_template: {"install_height_cm": {{ value }}}`; state from `<base>/config` |

## 8. Wi-Fi and TLS details (for battery life)

### Fast Wi-Fi reconnect

1. Use the cached **BSSID + channel** to skip the scan.
2. Use the cached **PMK** to skip PBKDF2, the 4096-iteration WPA2 key derivation. On an ARM968 this is likely
   1 s or more. Recompute and re-cache it only when the SSID or password changes.
3. Use a static IP if configured. Otherwise DHCP, and try requesting the cached lease address first.
4. If any of this fails, do a full scan + connect and refresh the cache.

Target: Wi-Fi associated with an IP address in **< 1.5 s** on the fast path. **⚠ measure.**

### TLS (mbedTLS)

- TLS 1.2 client, cipher order:
  1. `TLS-ECDHE-ECDSA-WITH-AES-128-GCM-SHA256`
  2. `TLS-ECDHE-RSA-WITH-AES-128-GCM-SHA256`
- **Recommend an ECDSA P-256 broker certificate.** An RSA-2048 handshake costs noticeably more CPU time on this
  core. **⚠ measure** both.
- Trust: the pasted CA only (`mbedtls_ssl_conf_ca_chain`), `MBEDTLS_SSL_VERIFY_REQUIRED`. Hostname check against
  `mqtt_host` when `mqtt_verify_host` is on. If you connect by IP, the certificate needs an IP SAN.
- Certificate dates: the device has no battery-backed clock.
  - Get the time by SNTP on the first connection after boot (mains) or once a day (battery). Between those, keep
    time by adding sleep durations.
  - If the time is unknown, skip only the date check (`MBEDTLS_X509_BADCERT_EXPIRED` / `_FUTURE`). Chain and
    hostname are always checked.
- Buffers: `MBEDTLS_SSL_IN_CONTENT_LEN 16384` (unless the broker supports max fragment length),
  `MBEDTLS_SSL_OUT_CONTENT_LEN 2048`. About 45 KB of heap at peak during the handshake. **⚠ check** free heap on
  BK7238 with Wi-Fi running.
- v2 option: TLS session tickets cached in run state, which cuts later handshakes to one round trip.
- `mqtt_tls = off`: plain TCP on 1883, with a warning on the page.

### Example broker setup (Mosquitto, own CA, ECDSA)

```bash
openssl ecparam -name prime256v1 -genkey -noout -out ca.key
openssl req -x509 -new -key ca.key -sha256 -days 3650 -subj "/CN=Home MQTT CA" -out ca.crt
openssl ecparam -name prime256v1 -genkey -noout -out broker.key
openssl req -new -key broker.key -subj "/CN=mqtt.home.lan" -out broker.csr
printf "subjectAltName=DNS:mqtt.home.lan,IP:192.168.1.10\n" > san.ext
openssl x509 -req -in broker.csr -CA ca.crt -CAkey ca.key -CAcreateserial -days 3650 -sha256 -extfile san.ext -out broker.crt
```

```
# /etc/mosquitto/conf.d/tls.conf
listener 8883
cafile   /etc/mosquitto/certs/ca.crt
certfile /etc/mosquitto/certs/broker.crt
keyfile  /etc/mosquitto/certs/broker.key
allow_anonymous false
password_file /etc/mosquitto/passwd
```

Paste the contents of `ca.crt` (not the key) into the setup page.

## 9. Power-down checklist (before every deep sleep)

1. MQTT `DISCONNECT`, close the socket, Wi-Fi off (`wifi stop` / RF off).
2. P26 LOW (head off).
3. UART1 TX (P11) → input without pull, or output LOW. RX (P10) → input with pull-down or no pull.
   **⚠ check** which gives the lowest current with the head unpowered.
4. LEDs P17 and P21 LOW.
5. P9: let the SDK put it in high-Z with the other unused pins. Keeping its pull-up measured the same
   (52.4 vs 52.2 µA).
6. P6: pull-up, wake on LOW.
7. Timer wake = `read_interval_min`, compensated for the time spent awake.
8. Flush the run-state record, then enter deep sleep.

Acceptance: whole-board sleep current ≤ **52 µA** at 4.2 V, as measured with the stock firmware.

## 10. Software architecture

```
apps/uls03gw/
  app.c           boot: wake reason, P9 power mode, button hold; battery, USB and setup modes
  board.c         GPIO, LEDs, button, deep sleep, awake cap, flash (guarded to 0x1FA000-0x200000)
  sensor.c        P26 power, UART1 9600, poll loop, uses core/uls_proto.c
  storage.c       config + run state on flash through core/abstore.c and core/runstate.c
  wifi.c          station connect, scan, setup AP
  net_mqtt.c      MQTT 3.1.1 session over plain TCP or mbedTLS, SNTP
  web.c           HTTP server and API for the setup page, captive DNS hook
  web/index.html  the setup page (gzipped into web_page.h at build time)
core/  (no SDK calls, unit-tested on a PC)
  uls_proto.c     sensor frame CRC, request, parse, trimmed mean
  level.c         maths from §3
  uls_policy.c    "should I send?" decision + back-off
  uls_config.c    settings: defaults, validation, JSON in/out
  abstore.c       A/B config sectors
  runstate.c      append-only run-state ring
  ha_discovery.c  discovery payloads + hash, state JSON
  json_mini.c     flat JSON parser and writer
  mqtt_pkt.c      MQTT packet encode/decode
  netproto.c      captive DNS, HTTP request head, SNTP
```

- Keep `level.c`, `uls_policy.c`, `uls_config.c` validation and `uls_proto.c` free of SDK calls, so they can be built and
  tested on a PC (`firmware/tests/`).
- Run everything as a single main task plus the SDK's Wi-Fi/lwIP tasks. Battery mode is a straight-line sequence
  that ends in sleep, not an event loop.
- No `printf` of SSIDs with passwords, MQTT passwords or keys. The stock firmware logs the Wi-Fi password; this
  one must not.

## 11. Toolchain, SDK, image and flashing

### SDK and toolchain (found 2026-11-22)

**SDK: `beken_freertos_sdk`**, https://github.com/NonPIayerCharacter/beken_freertos_sdk (note the capital "I" in
"NonPIayer").

- It is the Beken FreeRTOS SDK release (`beken_freertos_sdk_release`, 3.0.7x line) maintained by an OpenBeken developer.
- **OpenBeken builds its BK7238 firmware from it.** It is the `sdk/beken_freertos_sdk` submodule of
  openshwprojects/OpenBK7231T_App, target `OpenBK7238`, and those builds run on T1 modules. That makes it the
  best-proven open SDK for this chip.
- Alternatives checked:
  - Beken's official `bdk_freertos` lists BK7231U/N and BK7251, not BK7238.
  - Tuya's TuyaOpen doesn't list T1.
  - Commercial BK7238 SDKs (for example CIONLabs) are available only on request.

What it includes for the BK7238 (`beken378/app/config/sys_config_bk7238.h`):

| Need (this spec) | In the SDK |
|---|---|
| Build | `sh build.sh bk7238 <version>` → `make bk7238`. Bundled toolchain: `toolchain/arm-none-eabi-gcc.tar.xz` (extract first). Needs Linux, make, python3. |
| Deep sleep, GPIO + timer wake | `bk_enter_deep_sleep_mode(PS_DEEP_CTRL_PARAM*)` in `beken378/func/include/manual_ps_pub.h`. `wake_up_way = PS_DEEP_WAKEUP_GPIO \| PS_DEEP_WAKEUP_RTC`, `gpio_index_map = 1<<6`, `gpio_edge_map = 1<<6` (falling), `sleep_time` in seconds (capped at 0x1FFFF, ROSC). `gpio_stay_lo_map` = GPIO 0–31 the SDK leaves **as configured** (`_hi_` = GPIO 32–39). It does not force a level. Drive P26 and the LEDs LOW first, then list them. All other pins go high-Z. Wake reason: `bk_misc_get_start_type()`. `CFG_USE_DEEP_PS 1`. |
| Wi-Fi power save (mains mode) | `CFG_USE_STA_PS 1`, `CFG_USE_MCU_PS 1` (DTIM / MCU sleep) |
| TLS | mbedTLS **2.27.0** (`beken378/func/mbedtls`), `CFG_MBEDTLS 1`. lwIP 2.1.2 includes `altcp_tls_mbedtls`. |
| MQTT | Eclipse Paho embedded C (`beken378/func/paho-mqtt`) and an AT-server MQTT client. Demo: `demos/components/mqtt_demo`. lwIP's own `apps/mqtt` client also works over `altcp_tls`. |
| Setup AP | SoftAP + DHCP server (`beken378/func/lwip_intf/dhcpd`, which also has a small DNS responder for the captive portal). lwIP `httpd` is available, but a small custom HTTP handler is easier for POST/JSON. |
| WPA3 | `CFG_WPA3 1` (optional) |
| Packaging | `tools/beken_packager` (adds the 34-byte CRC blocks), output `out/bk7238_UA.bin` (app only) and `out/bk7238_QIO.bin` (with bootloader) |
| OTA image | `tools/rtt_ota/rt_ota_packaging_tool_cli` → `out/app.rbl` (gzip + AES, Beken default key `0123456789ABCDEF…`) |

Caveats:

- No license file. Beken sources are Apache-2.0 by header. The Wi-Fi/RF/BLE parts are binary `.a` blobs.
  This is fine for personal use; check before redistributing.
- Deep sleep through this SDK on BK7238 isn't used by OpenBeken. Confirmed here on hardware: P6 + RTC wake,
  ~52 µA sleep (§0).
- Its delay calibration on BK7238 is known to be off (OpenBeken carries workarounds in
  `src/hal/bk7231/hal_generic_bk7231.c`). This doesn't matter at 9600 baud.

**Bring-up order:**

1. Flash the OpenBeken BK7238 release `OpenBK7238_UA_<ver>.bin` to check flashing, boot and Wi-Fi.
2. Build the SDK's hello-world/demo.
3. Prove deep sleep + wake + sleep current.
4. Then build this firmware.

### Flash layout: stock (Tuya T1) vs SDK default

The two layouts **do not match**. Checked against the dump:

| Item | Stock dump (Tuya) | SDK default, 2 MB (`beken378/func/user_driver/BkDriverFlash.c`) | Action |
|---|---|---|---|
| Bootloader | 0x000000–0x011000, **Tuya "diff2ya" bootloader** (differential/xz OTA) | 0x000000, Beken v1.0.15 | **keep Tuya's; never rewrite** (it also handles UART download) |
| App | 0x011000 (logical 0x10000) | 0x011000, length 0x121000 | same, compatible |
| OTA download | 0x0FE000–0x1E3000 (empty) | 0x132000, length 0xAE000 | see OTA below |
| RF calibration (+ MAC) | **0x1E3000**: Beken `TLV\0` format | **0x1E0000** (empty on this board) | **change `BK_PARTITION_RF_FIRMWARE` to 0x1E3000** so the SDK uses the factory calibration and MAC |
| Net params / fast connect | (0x1E1000 empty) | 0x1E1000 | fine, free space |
| Tuya data | 0x1E4000–0x1E8000 (Tuya config, contains secrets) | unused | leave alone, or erase after backup |
| Tuya KV | 0x1F5000–0x200000 (encrypted) | BLE bonding at 0x1F0000 (BLE off) | erase after backup; use for config + run state (§4) |

- If the RF partition isn't moved, the SDK starts without factory calibration (default TX power and crystal trim,
  likely a generic MAC). It may work, but range and frequency accuracy suffer.
- Confirmed on hardware: the SDK's TLV parser accepts Tuya's block (factory MAC and calibration load).
- App image format: the stock bootloader expects 34-byte CRC blocks with a plaintext app (the bootloader's
  encryption key is effectively null). The SDK's `bk7238_UA.bin` is this format, and OpenBeken flashes it at
  0x11000 on T1 modules.

### Flashing

1. **Keep the full 2 MB backup of the stock dump** (`readResult_BK7238_QIO_*.bin`). It holds the original firmware
   and keys. Never share it.
2. SW2 off, **sensor head unplugged** (it shares pads 15/16), external 3.3 V on pad 14, adapter at 3.3 V logic.
   Wiring is in the [README](../README.md) §2.
3. BK7231 Easy UART Flasher (BK7231GUIFlashTool), mode **BK7238**. Write the **UA (app-only) image at 0x11000**.
   **Do not write a QIO/full image**: it replaces the bootloader.
4. On first install, erase 0x1F5000–0x200000 (old Tuya KV).
5. Disconnect the adapter (it back-powers the board), reconnect the head, power from the battery input.
6. Debug log on pad 7 (UART2 TX) at 115200 needs only GND + RX from an adapter. Unplug it after debugging too.

### OTA (v2)

- The stock bootloader is Tuya's "diff2ya" variant. It logs `_judge_ota_info`, `diff2ya_header info`, `patch type`
  and `ota successd!`.
- **⚠ verify** whether it also accepts a standard Beken `.rbl` placed in its download area, and where that area is.
  Either analyse Tuya's bootloader at flash 0x0 (`pht_addr`, `_judge_ota_info`) or try an OpenBeken `.rbl` OTA on
  this board while a UART recovery path is available.
- Until then, update over UART only. The web page OTA endpoint stays disabled.

## 12. Open items / hardware to-do

- [x] Sensor warm-up: trigger at ≥ 500 ms after P26 HIGH, valid reading 500 ms later (§2).
- [x] Response bytes 11–12: constant `3F AC`, not a CRC. Bytes 3–4 echo the request, 7–9 are `FF`.
- [ ] Byte 10: very likely °C (28 in a 28–29 °C room); confirm at another temperature.
- [x] Flashing, boot and Wi-Fi with a UA image at 0x11000.
- [x] Move `BK_PARTITION_RF_FIRMWARE` to 0x1E3000 and confirm the SDK reads Tuya's TLV (MAC + calibration).
- [x] Prove `bk_enter_deep_sleep_mode` on BK7238: GPIO P6 + RTC wake, ~52 µA sleep.
- [ ] Work out the Tuya diff2ya bootloader's OTA format and download address.
- [ ] Confirm that deep sleep keeps no RAM (if some is retained, keep run state there instead of flash).
- [ ] Measure the current profile of one TLS send (shunt + scope, or a USB power meter on a bench supply).
- [x] Measure boot time from a button wake to `user_main` (~0.26 s), for hold timing.
- [ ] **Battery voltage:** the board doesn't measure it. Option: a 1 MΩ / 1 MΩ divider from the battery to the
      ADC pad (pad 5, P20), with a 100 nF capacitor. It draws ~2 µA at 4.2 V. Read it once per send and add
      `battery_v` to the state and discovery. Optional hardware mod.
- [ ] Identify the "14VF" regulator (only matters if sleep current is to drop below ~50 µA).
- [ ] Solar option: CN3065 / CN3791 / TP4056+DW01 charger into the **battery connector**, not USB (USB pulls P9 LOW
      and the device never sleeps). Add an NTC for no charging below 0 °C.

## 13. Acceptance tests

| # | Test | Pass |
|---|---|---|
| 1 | Fresh flash, no config | AP `ULS-03GW-XXXXXX` appears, captive page opens on a phone |
| 2 | Save Wi-Fi + MQTT (TLS, pasted CA) and use "Test" | step results all OK; a test message arrives at the broker |
| 3 | Wrong CA pasted | "Test" reports a certificate verification failure; no plain-text fallback |
| 4 | Battery, timer wake, level unchanged | no Wi-Fi activity (current stays ~26–30 mA, never the ~60 mA TX level), back asleep in < 6 s |
| 5 | Level change > 2 cm | state published, QoS 1 acked, asleep within ~10 s of wake |
| 6 | Heartbeat elapsed | state published although unchanged |
| 7 | Short button press | reading sent immediately; blue LED flashes once |
| 8 | Hold 4 s | AP mode; closes after 5 min idle and the device sleeps |
| 9 | Hold 15 s | factory reset, AP mode with defaults |
| 10 | Broker down | back-off works; awake time per wake stays under the 45 s cap; reads continue |
| 11 | Retained `<base>/set` with `install_height_cm` | applied on the next send, `<base>/config` updated, `/set` cleared |
| 12 | Home Assistant | device appears with all entities; battery mode shows a value, not "unavailable", between heartbeats |
| 13 | USB power (P9 LOW) | stays connected, `status` online, reads every 10 s, LWT shows offline when unplugged |
| 14 | Sleep current | ≤ 52 µA at 4.2 V, whole board, head attached, TTL adapter removed |
| 15 | Height == full distance | `configured: false`, `level_pct: null`, no crash |
| 16 | Logs | no SSID password, MQTT password or CA private data printed on UART2 |

## 14. Sources

- SDK: https://github.com/NonPIayerCharacter/beken_freertos_sdk (BK7238 target, mbedTLS 2.27, Paho MQTT, deep sleep API)
- OpenBeken app and BK7238 build recipe (`Makefile` target `OpenBK7238`, `.gitmodules`): https://github.com/openshwprojects/OpenBK7231T_App
- OpenBeken BK7238 release assets (UA / QIO / RBL): https://newreleases.io/project/github/openshwprojects/OpenBK7231T_App/release/1.18.308
- Flasher: https://github.com/openshwprojects/BK7231GUIFlashTool
- Official Beken bdk_freertos chip list (no BK7238): https://gitee.com/gitesin/bdk_freertos
- Commercial BK7238 SDK access: https://cionlabs.com/?p=2444
- TuyaOpen (no T1 listed): https://github.com/tuya/TuyaOpen
- T1 / BK7238 module notes: https://www.elektroda.com/news/news4132679.html
- BK7238 build reports: https://www.elektroda.com/rtvforum/topic4092465-240.html

Quick way to inspect the SDK without the full 235 MB clone:

```bash
git clone --depth 1 --filter=blob:none --sparse https://github.com/NonPIayerCharacter/beken_freertos_sdk.git sdk
cd sdk && git sparse-checkout set --no-cone /build.sh /Makefile /application.mk /beken378/app/config \
  /tools/beken_packager /beken378/func/user_driver /beken378/func/power_save /beken378/func/include
```
