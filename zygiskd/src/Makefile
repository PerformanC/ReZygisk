CC := ~/Android/Sdk/ndk/27.0.11902837/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android34-clang

# FILES = root_impl/common.c   \
#         root_impl/kernelsu.c \
# 				companion.c          \
# 				dl.c                 \
# 				main.c               \
# 				utils.c              \
# 				zygiskd.c

FILES = root_impl/*.c \
        *.c

CFLAGS = -D_GNU_SOURCE -std=c99 -Wpedantic -Wall -Wextra -Werror -Wformat -Wuninitialized -Wshadow -Wno-zero-length-array -Wno-fixed-enum-extension -Iroot_impl -llog

all: CFLAGS += -DDEBUG=0 -O3 -flto=thin -Wl,--strip-all
all:
	$(CC) $(CFLAGS) $(FILES) -o zygiskd64

debug: CFLAGS += -DDEBUG=1 -g -O0
debug:
	$(CC) $(CFLAGS) $(FILES) -o zygiskd64

32bit: CFLAGS += -m32 -DDEBUG=0 -O3 -flto=thin -Wl,--strip-all
32bit:
	$(CC) $(CFLAGS) $(FILES) -o zygiskd32

32bit-debug: CFLAGS += -m32 -DDEBUG=1 -g -O0
32bit-debug:
	$(CC) $(CFLAGS) $(FILES) -o zygiskd32

clean:
	rm -f zygiskd