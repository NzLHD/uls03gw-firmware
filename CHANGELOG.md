# Changelog

Images are attached to the [GitHub Releases](https://github.com/NzLHD/uls03gw-firmware/releases). Flash the app-only
`uls03gw_UA_<version>.bin` at **0x11000**; never write a full/QIO image.

## 0.2.3-rc1 (pre-release, being tested)

- A TLS or TCP write to a dead connection no longer retries forever: writes give up after 5 s. Before, USB
  mode could freeze after a broker restart or Wi-Fi drop until it was power-cycled.
- USB mode notices a broker that stops answering (no reply to pings for 1.5 × keepalive, or a missing PUBACK),
  reconnects and resends.
- A software watchdog reboots the device if the USB-mode loop or the setup page is stuck for 180 s.
- Battery mode: the 45 s awake cap no longer writes flash while the stuck code might be writing it.
- Battery mode: a deep-sleep wake reported as power-on (no button bit) counts as a timer wake, so the clock and
  the heartbeat keep running.
- The debug log shows the raw wake registers and, for every reading, why the device did or didn't send.

## 0.2.2

- TLS certificate dates are checked against a fresh SNTP time. Before, a stale time carried over a reboot could
  make normal sends fail with "not yet valid".
- The Wi-Fi passphrase and session keys printed by the SDK's closed supplicant library are hidden from the log.
- The setup page opens by itself (captive DNS through the SDK's DHCP server).

## 0.2.1

- RSSI is reported in battery mode.
- The retained USB-mode `status` topic is cleared after switching to battery.

## 0.2.0

- First full firmware: setup access point and web page, battery and USB modes, MQTT over TLS or plain TCP,
  Home Assistant discovery.
