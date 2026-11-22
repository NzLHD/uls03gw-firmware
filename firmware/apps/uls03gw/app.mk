# ULS-03GW clean firmware (firmware/apps/uls03gw). Included by sdk_glue.mk.
# The setup page is gzipped into web_page.h by scripts/build.sh (web/gen_page.py).
UA := $(ULS_DIR)/apps/uls03gw
INCLUDES += -I$(ULS_DIR)/core -I$(UA)

APP_C += $(UA)/app.c $(UA)/board.c $(UA)/sensor.c $(UA)/storage.c $(UA)/wifi.c \
         $(UA)/net_mqtt.c $(UA)/web.c
APP_C += $(ULS_DIR)/core/uls_proto.c $(ULS_DIR)/core/level.c $(ULS_DIR)/core/uls_policy.c \
         $(ULS_DIR)/core/json_mini.c $(ULS_DIR)/core/uls_config.c $(ULS_DIR)/core/abstore.c \
         $(ULS_DIR)/core/runstate.c $(ULS_DIR)/core/ha_discovery.c $(ULS_DIR)/core/mqtt_pkt.c \
         $(ULS_DIR)/core/netproto.c
