# J5 native decoders — host build + self-test harness.
#
# Build inside the host container:
#   docker/host-build.sh make -C port/src/codec
#
# The codec library (adx.c aix.c afs.c movie000.c) is standalone portable C99:
# no stdio, no malloc, no platform headers — it must later cross-compile for
# the Vita with vitasdk unchanged. Only the harness (codec_selftest.c,
# sha256.c, png_write.c, pl_mpeg) may use host facilities.

CC      ?= gcc
OPT     ?= -O2
CFLAGS  ?= $(OPT) -std=c99 -pedantic -Wall -Wextra -Werror
PLM_DIR := ../third_party/pl_mpeg
BUILD   := build
LDLIBS  ?= -lm

LIB_OBJS := $(BUILD)/adx.o $(BUILD)/aix.o $(BUILD)/afs.o $(BUILD)/movie000.o
HARNESS_OBJS := $(BUILD)/codec_selftest.o $(BUILD)/sha256.o $(BUILD)/png_write.o \
                $(BUILD)/pl_mpeg_impl.o

all: $(BUILD)/codec_selftest

$(BUILD)/codec_selftest: $(LIB_OBJS) $(HARNESS_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LDLIBS)

$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) $(CFLAGS) -I. -o $@ -c $<

$(BUILD)/codec_selftest.o: codec_selftest.c | $(BUILD)
	$(CC) $(CFLAGS) -I. -I$(PLM_DIR) -o $@ -c $<

$(BUILD)/pl_mpeg_impl.o: pl_mpeg_impl.c | $(BUILD)
	$(CC) $(OPT) -std=gnu99 -w -I$(PLM_DIR) -o $@ -c $<

$(BUILD):
	mkdir -p $(BUILD)

# Cross-compile the shipped codec library for the Vita (proof of portability;
# run inside the vita-sdk container, e.g.
#   docker run --rm -v <repo>:/work -u $(id -u):$(id -g) -w /work \
#     sh3vita-sdk:local make -C port/src/codec vita VITASDK=/usr/local/vitasdk)
VITASDK ?= /usr/local/vitasdk
VITA_CC := $(VITASDK)/bin/arm-vita-eabi-gcc
VITA_AR := $(VITASDK)/bin/arm-vita-eabi-ar
VITA_BUILD := $(BUILD)/vita
VITA_OBJS := $(VITA_BUILD)/adx.o $(VITA_BUILD)/aix.o $(VITA_BUILD)/afs.o $(VITA_BUILD)/movie000.o

vita: $(VITA_BUILD)/libsh3codec.a

$(VITA_BUILD)/%.o: %.c | $(VITA_BUILD)
	$(VITA_CC) $(CFLAGS) -I. -o $@ -c $<

$(VITA_BUILD)/libsh3codec.a: $(VITA_OBJS)
	$(VITA_AR) rcs $@ $^

$(VITA_BUILD):
	mkdir -p $(VITA_BUILD)

clean:
	rm -rf $(BUILD)

.PHONY: all clean vita
