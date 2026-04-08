ROOT_DIR := ..
include ../common.mk

OBJ_ROOT = $(BUILD_DIR)/obj/$(BUILD_TYPE)/zygiskd
OBJ_DIR = $(OBJ_ROOT)/$(ARCH)
STAMP = $(OBJ_ROOT)/.built

ZYGISKD_CFLAGS = -D_GNU_SOURCE -std=c99 -Wpedantic -Wall -Wextra -Werror -Wformat -Wuninitialized -Wshadow -Wno-zero-length-array -Wno-fixed-enum-extension -Wno-unknown-warning-option -Wno-c23-extensions -Wconversion -DMIN_APATCH_VERSION=$(MIN_APATCH_VERSION) -DMIN_KSU_VERSION=$(MIN_KSU_VERSION) -DMIN_MAGISK_VERSION=$(MIN_MAGISK_VERSION) '-DZKSU_VERSION="$(VER_NAME)"'
ifeq ($(BUILD_TYPE),release)
	ZYGISKD_TYPE_CFLAGS = -O3 -ffast-math
	ZYGISKD_TYPE_LDFLAGS = -Wl,--strip-all -flto=thin
else
	ZYGISKD_TYPE_CFLAGS = -g -O0 -DDEBUG
	ZYGISKD_TYPE_LDFLAGS =
endif

INPUTS = Makefile ../common.mk $(shell find src -type f | sort)
SRCS = src/root_impl/apatch.c src/root_impl/common.c src/root_impl/kernelsu.c src/root_impl/magisk.c src/companion.c src/main.c src/utils.c src/zygiskd.c
OBJS = $(patsubst src/%.c,$(OBJ_DIR)/%.o,$(SRCS))
DEPS = $(OBJS:.o=.d)
BIN = $(OBJ_DIR)/zygiskd
INCLUDES = -Isrc/root_impl

.PHONY: all arch clean

all: $(STAMP)

arch: $(BIN)

clean:
	rm -rf $(OBJ_ROOT)

$(STAMP): $(INPUTS)
	@for arch in $(ARCHS); do \
		$(MAKE) BUILD_TYPE=$(BUILD_TYPE) ARCH=$$arch arch; \
	done
	@mkdir -p $(dir $@)
	@touch $@

$(OBJ_DIR)/%.o: src/%.c Makefile ../common.mk
	@mkdir -p $(dir $@)
	$(CC_ARCH) $(ZYGISKD_CFLAGS) $(ZYGISKD_TYPE_CFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

$(BIN): $(OBJS)
	@mkdir -p $(dir $@)
	$(CC_ARCH) $(ZYGISKD_TYPE_LDFLAGS) $(OBJS) -llog -o $@

-include $(DEPS)
