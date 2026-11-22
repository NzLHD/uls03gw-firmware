# Contributing

Bug reports, hardware results and pull requests are welcome.

## Reporting a problem

Use the issue template. The most useful details are:

- the hardware version printed on the board (for example V1.3) and where the device was bought;
- the firmware version (shown on the setup page and in the `state` message);
- power mode (battery or USB) and whether MQTT uses TLS;
- the debug log from pad 7 at 115200 baud. Passwords and keys are never printed, but **remove your SSID, broker
  address and MAC** before posting.

Never post a flash dump: it holds your device's Tuya keys and Wi-Fi credentials.

## Changing the code

- `firmware/core/` must stay free of SDK calls so it can be tested on a PC. Add or update tests in
  `firmware/tests/` and run `firmware/tests/run.sh` (unit tests under ASan/UBSan plus an ARM968 `-Werror`
  compile). CI runs the same checks.
- Changes to the Beken SDK go in `firmware/sdk-patches/` as patches, never as edits to a checked-in SDK.
- Anything that touches RF calibration or TX power: check that `manual_cal_update_flash_area` is not linked
  (see [firmware/README.md](firmware/README.md#safety-check)). It is the only code that can rewrite the
  factory RF/MAC block.
- Do not copy code from projects with incompatible licenses (OpenBeken is GPL). Learning from them is fine.
- Do not add Tuya firmware, decompiled Tuya code or flash dumps to the repository.

Describe hardware tests in the pull request: what was flashed, what was checked, what was measured.
