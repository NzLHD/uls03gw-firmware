# Changelog

Images are attached to the [GitHub Releases](https://github.com/NzLHD/uls03gw-firmware/releases). Flash the app-only
`uls03gw_UA_<version>.bin` at **0x11000**; never write a full/QIO image.

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
