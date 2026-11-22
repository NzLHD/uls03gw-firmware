# firmware/: developer notes

The design is in [../docs/spec.md](../docs/spec.md), the hardware in [../docs/hardware.md](../docs/hardware.md).

```
firmware/
  apps/uls03gw/   the firmware: app.c (battery / USB / setup modes), board, sensor, storage, wifi,
                  net_mqtt (MQTT over mbedTLS), web (setup AP + HTTP API), web/index.html (gzipped at build)
  core/           portable modules, no SDK calls, unit-tested on a PC:
                  level, uls_policy (send decision + back-off), uls_config (settings, validation, JSON),
                  json_mini, abstore (A/B config store), runstate (ring across deep sleep),
                  ha_discovery (+ state JSON), mqtt_pkt, netproto (captive DNS, HTTP, SNTP), uls_proto
  tests/          PC unit tests (ASan/UBSan) + ARM compile check
  glue/           SDK stubs, log filter (hides keys), uls_mbedtls_user.h (enables the TLS client)
  sdk-patches/    0001 build hook · 0002 RF/MAC partition at 0x1E3000 · 0003 GPIO wake status with
                  Tuya's bootloader · 0004 captive DNS via the SDK's DHCP server
  scripts/build.sh  applies the patches and builds in the Docker container (tools/buildenv/)
```

## Build and test

```bash
git clone --depth 1 https://github.com/NonPIayerCharacter/beken_freertos_sdk.git sdk/beken_freertos_sdk
firmware/tests/run.sh                    # 445 checks
firmware/scripts/build.sh uls03gw        # -> firmware/out/uls03gw/bk7238_UA.bin (flash at 0x11000)
```

- The SDK (commit dfc1828) is never edited by hand; `build.sh` applies `sdk-patches/`.
- The bundled GCC 15.2 is extracted on first use.
- `ULS_DEFINES="-D..."` adds build defines, and `ULS_OUT=name` picks the output folder.

## SDK pitfalls (beken_freertos_sdk, BK7238)

- **UART1:**
  - It has no RX FIFO, so `bk_uart_recv()` never returns data. Drain it in a `bk_uart_set_rx_callback()`
    callback.
  - `bk_uart_send()` stops at the first NUL byte. Use `bk_send_byte()` for binary data.
  - Pins parked as GPIO need `CMD_GPIO_ENABLE_SECOND`/`GFUNC_MODE_UART1` to become UART again.
- **Deep sleep:**
  - `gpio_stay_lo_map` means "GPIO 0–31: leave as configured", not "drive LOW".
  - Tuya's bootloader doesn't save the GPIO wake status (patch 0003).
- **mbedTLS:** the active `tls_config.h` has no TLS protocol. `glue/uls_mbedtls_user.h` adds it via
  `MBEDTLS_USER_CONFIG_FILE`, and `build.sh` rebuilds libmbedtls when that file changes.
- **Logs:** the closed `libsupplicant` prints the SSID + passphrase and session keys via `bk_printf`.
  `--wrap=bk_printf` + `glue/log_filter.c` hide them.
- **Captive portal:** UDP 53 on the AP belongs to the SDK's DHCP server, which answers REFUSED. Patch 0004
  hands queries to `uls_dns_reply()`.
- **Header names:** SDK include paths come first. That's why ours are `uls_config.h` / `uls_policy.h`
  (wpa_supplicant has a `config.h`).
- **Build status:** the SDK's `build.sh` ignores a failed `make`. Ours runs it with `sh -e` and removes old
  outputs first.
- **LTO:** `strchr`/`strcpy` must be forced in with `-u` (see `sdk_glue.mk`).

## Safety check

The calibration blob's only flash writer must not be linked: otherwise the factory RF/MAC block at 0x1E3000
could be rewritten.

```bash
sdk/beken_freertos_sdk/toolchain/arm-none-eabi/bin/arm-none-eabi-nm firmware/out/uls03gw/bk7238.elf | grep manual_cal_update_flash_area   # expect nothing
```

The firmware writes flash only in 0x1FA000–0x200000 (guarded in `apps/uls03gw/board.c`).

## Releasing

Images are GitHub Release assets, never committed.

1. Bump the version, build with `firmware/scripts/build.sh uls03gw <version>`, and run the safety check above.
2. Copy `firmware/out/uls03gw/bk7238_UA.bin` to `uls03gw_UA_<version>.bin` and write `SHA256SUMS`.
3. Add the changes to [../CHANGELOG.md](../CHANGELOG.md), tag `v<version>`, and attach both files to the release.

Never publish the QIO image, the ELF or the map file.

CI (`.github/workflows/ci.yml`) runs the unit tests and the ARM compile check with Ubuntu's
`gcc-arm-none-eabi`; it does not build the SDK image.
