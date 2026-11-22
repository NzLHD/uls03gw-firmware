# ULS-03GW hardware notes

Board `T1-U_liquid_sensor HW: V1.3`, sold as ULS-03GW / CSB-03GW (HojellyTek, Tuya / Smart Life).

| Item | Value |
|---|---|
| Module | Tuya **T1-U-HL**, Beken **BK7238** (ARM968E-S), 2 MB flash |
| Power | battery (3× 18650, or 1× 18650) → SW2 → buck → 3.3 V, or 5 V USB |
| Sensor head | 4-pin `+ G TX RX` header: serial ultrasonic head, 9600 8N1, powered via P26 |
| Stock firmware | Tuya "T1-U_liquid_level_sensor" 1.2.22 (TuyaOS) with Tuya's "diff2ya" bootloader |

## T1-U-HL pads used

| Pad | Signal | Use |
|---|---|---|
| 7 | P0 / TX2 | **debug log** out, 115200 8N1 |
| 9 | P9 | power mode in: HIGH = battery, LOW = USB |
| 10 | P26 | sensor head power out (HIGH = on) |
| 11 | P6 | button in (active LOW, pull-up), deep-sleep wake |
| 13 | GND | |
| 14 | 3V3 | supply |
| 15 | P11 / TX1 | **flashing** TX, and sensor head UART |
| 16 | P10 / RX1 | **flashing** RX, and sensor head UART |
| 18 | CEN | reset (active LOW) |
| 19 | P21 | blue LED (network), active HIGH |
| 20 | P17 | red LED (power), active HIGH |

Pads 15/16 are shared by the flasher and the sensor head: unplug the head while flashing.

## Power (measured at 4.2 V on the battery connector, whole board)

| State | Current |
|---|---|
| Deep sleep, head attached | 52.2 µA (stock: 51.6 µA) |
| Taking a reading (~1 s) | 16 mA |
| Awake, Wi-Fi on | ~26 mA |
| Setup AP | ~56 mA |

A USB-TTL adapter left on pads 15/16 back-powers the board. Remove it before measuring.
Measuring: short the µA meter while it boots, or use a 100 Ω shunt + 1N4148 bypass.

## Sensor head protocol

- Request (8 bytes): `18 08 6A FF FF 27 CE 0D`
- Reply (14 bytes): `18 0E 6A FF FF DL DH FF FF FF TT 3F AC 0D`
  - distance = `DH<<8 | DL` mm, and 0 means no echo.
  - `TT` ≈ temperature in °C.
  - `3F AC` is a constant, not a CRC.
- Each request starts a measurement; the next reply carries its result.
- Requests must be ≥ ~450 ms apart. A request needs ≥ ~500 ms after power-on to give a valid result.
- The head sends 5 junk bytes at power-on.
- Fastest reading: power on, trigger at 500 ms, read at 1000 ms. Details in [spec.md](spec.md) §2.

## Flash layout

| Address | Stock (Tuya) | This firmware |
|---|---|---|
| 0x000000 | Tuya bootloader | **kept** (never overwrite) |
| 0x011000 | Tuya app | app (UA image) |
| 0x1E3000 | factory RF calibration + MAC (`TLV`) | read as-is |
| 0x1E4000–0x1E8000 | Tuya data (device keys) | untouched |
| 0x1F5000–0x200000 | Tuya KV store (encrypted, device keys) | config 0x1FA000/0x1FC000, run state 0x1FE000/0x1FF000 |

App images use 34-byte blocks (32 data + CRC-16, poly 0x8005, init 0xFFFF, big-endian).
