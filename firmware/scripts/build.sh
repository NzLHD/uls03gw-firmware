#!/bin/sh
# Build a BK7238 image with the Beken SDK, inside the uls03gw-build container.
#
#   firmware/scripts/build.sh [app] [version]
#
#   app      folder under firmware/apps/ (default: uls03gw); "sdk_demo" = SDK default app
#   version  USER_SW_VER string (default 0.0.1)
#
# Environment (optional):
#   ULS_DEFINES  extra -D flags for our sources, e.g. "-DSLEEP_TEST_SLEEP_S=600"
#   ULS_OUT      output folder name under firmware/out/ (default: <app>)
#
# Output: firmware/out/<app>/  (bk7238_UA.bin = app-only image for 0x11000, plus .elf/.map)
# Never flash bk7238_QIO.bin: it contains the SDK bootloader and would replace Tuya's.
set -e
APP=${1:-uls03gw}
VER=${2:-0.0.1}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
SDK=$ROOT/sdk/beken_freertos_sdk
FW=$ROOT/firmware

[ -d "$SDK/beken378" ] || { echo "SDK not found at $SDK (see firmware/README.md)"; exit 1; }
[ -x "$SDK/toolchain/arm-none-eabi/bin/arm-none-eabi-gcc" ] || \
    (cd "$SDK/toolchain" && tar xJf arm-none-eabi-gcc.tar.xz)

# Apply our SDK patches once (idempotent).
for p in "$FW"/sdk-patches/*.patch; do
    if git -C "$SDK" apply --reverse --check "$p" 2>/dev/null; then
        :   # already applied
    else
        echo "applying $(basename "$p")"
        git -C "$SDK" apply "$p"
    fi
done

# The SDK compiles our sources through this link (keeps objects inside out/).
ln -sfn ../../firmware "$SDK/uls"

if [ "$APP" = "sdk_demo" ]; then ULS_APP=; else ULS_APP=$APP; fi
[ -z "$ULS_APP" ] || [ -f "$FW/apps/$ULS_APP/app.mk" ] || { echo "no firmware/apps/$ULS_APP/app.mk"; exit 1; }

# Apps with a setup page: gzip it into a C header.
if [ -n "$ULS_APP" ] && [ -f "$FW/apps/$ULS_APP/web/gen_page.py" ]; then
    python3 "$FW/apps/$ULS_APP/web/gen_page.py" "$FW/apps/$ULS_APP/web/index.html" "$FW/apps/$ULS_APP/web_page.h"
fi

# The SDK only tracks header dependencies, so switching apps needs a clean of libapp.
rm -f "$SDK/libapp.a"
rm -rf "$SDK/out/uls"
# make doesn't track compiler flags: rebuild mbedTLS when our mbedTLS additions change
STAMP="$SDK/out/.uls_mbedtls_stamp"
SUM=$(sha256sum "$FW/glue/uls_mbedtls_user.h" | cut -c1-16)
if [ "$(cat "$STAMP" 2>/dev/null)" != "$SUM" ]; then
    echo "mbedTLS config changed: rebuilding libmbedtls"
    rm -rf "$SDK/out/beken378/func/mbedtls" "$SDK/libmbedtls.a"
    mkdir -p "$SDK/out" && echo "$SUM" > "$STAMP"
fi
# never copy a previous build's image after a failed one
rm -f "$SDK/out/bk7238_UA.bin" "$SDK/out/bk7238.elf" "$SDK/out/bk7238.map" "$SDK/out/app.rbl"

SUBDIR=sdk/beken_freertos_sdk "$ROOT/tools/buildenv/run.sh" sh -c "
    export ARM_GCC_TOOLCHAIN=\$PWD/toolchain/arm-none-eabi/bin/
    export ULS_GLUE_MK=./uls/sdk_glue.mk ULS_APP='$ULS_APP' ULS_DEFINES='${ULS_DEFINES:-}'
    sh -e build.sh bk7238 '$VER'"   # -e: the SDK script otherwise ignores a failed make

OUT=$FW/out/${ULS_OUT:-$APP}
mkdir -p "$OUT"
for f in bk7238_UA.bin bk7238.elf bk7238.map app.rbl; do
    [ -f "$SDK/out/$f" ] && cp "$SDK/out/$f" "$OUT/"
done
[ -f "$OUT/bk7238_UA.bin" ] || { echo "build produced no bk7238_UA.bin"; exit 1; }
"$SDK/toolchain/arm-none-eabi/bin/arm-none-eabi-size" "$OUT/bk7238.elf"
echo "OK: $OUT/bk7238_UA.bin ($(stat -c %s "$OUT/bk7238_UA.bin") bytes) - flash at 0x11000"
