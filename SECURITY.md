# Security

Please report security problems privately through GitHub's
[private vulnerability reporting](https://github.com/NzLHD/uls03gw-firmware/security/advisories/new) rather than in a
public issue.

In scope: the setup access point and its HTTP API, MQTT and TLS handling, settings storage, and anything that
could leak the Wi-Fi password, MQTT credentials or CA, or damage the factory RF/MAC block.

Known design limits:

- The setup access point has a default password, `uls03gw-setup`, until you change it. Setup mode only starts
  when the button is held for 4 s or when no Wi-Fi is configured.
- Plain (non-TLS) MQTT is allowed as an option; use TLS on any network you don't control.
- Flash is not encrypted: anyone with physical access and a UART adapter can read the stored settings.
