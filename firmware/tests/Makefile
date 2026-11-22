# PC-side unit tests for the portable firmware modules (firmware/core).
#   make            build + run the tests on the host (ASan + UBSan)
#   make cross      compile firmware/core for the BK7238 (ARM968E-S) with -Werror
# Normally run through ./run.sh, which uses the uls03gw-build container.

CORE    := ../core
SRCS    := $(CORE)/uls_proto.c $(CORE)/level.c $(CORE)/uls_policy.c $(CORE)/json_mini.c \
           $(CORE)/uls_config.c $(CORE)/abstore.c $(CORE)/runstate.c $(CORE)/ha_discovery.c \
           $(CORE)/mqtt_pkt.c $(CORE)/netproto.c
TESTS   := test_main.c test_uls_proto.c test_level.c test_policy.c test_json.c \
           test_config.c test_abstore.c test_runstate.c test_ha.c test_net.c ramflash.c

CC      ?= gcc
CFLAGS  := -std=c99 -O1 -g -Wall -Wextra -Werror -Wshadow \
           -fsanitize=address,undefined -fno-sanitize-recover=all -I$(CORE)

ARM_CC  ?= ../../sdk/beken_freertos_sdk/toolchain/arm-none-eabi/bin/arm-none-eabi-gcc
ARM_SIZE ?= $(dir $(ARM_CC))arm-none-eabi-size
ARM_CFLAGS := -std=c99 -Os -mthumb -mcpu=arm968e-s -march=armv5te -mthumb-interwork \
           -ffunction-sections -fdata-sections -fsigned-char -Wall -Wextra -Werror -I$(CORE)

.PHONY: test cross clean
test: unit_tests
	./unit_tests

unit_tests: $(SRCS) $(TESTS) $(wildcard $(CORE)/*.h) unit.h ramflash.h
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(TESTS)

cross:
	@mkdir -p obj-arm
	@for f in $(SRCS); do $(ARM_CC) $(ARM_CFLAGS) -c $$f -o obj-arm/$$(basename $$f .c).o || exit 1; done
	@$(ARM_SIZE) obj-arm/*.o

clean:
	rm -rf unit_tests obj-arm
