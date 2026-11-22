# sdk_glue.mk - included by the Beken SDK's application.mk (see sdk-patches/0001).
#
# Paths are relative to the SDK root; scripts/build.sh creates the symlink
#   <sdk>/uls -> <project>/firmware
# so objects land under the SDK's out/ tree instead of escaping it.
#
# Variables (from the environment, set by scripts/build.sh):
#   ULS_APP   name of a folder under firmware/apps/ to build (empty = SDK default app)

ULS_DIR := ./uls

INCLUDES += -I$(ULS_DIR)/include -I$(ULS_DIR)/glue

# Enable the TLS client in the SDK's mbedTLS (see glue/uls_mbedtls_user.h)
CFLAGS += -DMBEDTLS_USER_CONFIG_FILE=\"uls_mbedtls_user.h\"

# Use the Tuya T1 flash layout: RF calibration + MAC partition at 0x1E3000
# (sdk-patches/0002). The factory TLV there carries the MAC and RF calibration.
CFLAGS += -DULS03GW_TUYA_T1_LAYOUT
# Behaviour patches for this firmware (sdk-patches/0004: DHCP server advertises our DNS)
CFLAGS += -DULS03GW_FIRMWARE

# Extra build-time defines from scripts/build.sh (ULS_DEFINES).
CFLAGS += $(ULS_DEFINES)

# Weak BLE stubs shipped with the SDK (BLE is disabled on BK7238 builds, but the
# Wi-Fi/cal blobs still reference a few BLE coexistence symbols).
SRC_C += ./fixes/blank.c
# Our own small stubs (PHY 11b low-power hooks, lwIP hostname hook).
SRC_C += $(ULS_DIR)/glue/sdk_stubs.c
# Hide Wi-Fi passphrase / key dumps printed by the closed supplicant library
SRC_C += $(ULS_DIR)/glue/log_filter.c
LFLAGS += -Wl,--wrap=bk_printf

# With -flto, references to strchr/strcpy only appear after LTRANS, when ld has
# already finished with newlib's libg, so they stay undefined (GCC 15 toolchain).
# Force them in during the first pass and keep libg/libgcc inside the link group.
LFLAGS += -Wl,-u,strchr -Wl,-u,strcpy
LIBFLAGS += -lg -lgcc

ifneq ($(strip $(ULS_APP)),)
include $(ULS_DIR)/apps/$(ULS_APP)/app.mk
endif
