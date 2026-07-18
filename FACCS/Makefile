# Makefile — amodem+FEC decoder in C
# native build:
#   make
# ARM Linux cross:
#   make ARCH=arm CROSS=arm-linux-gnueabihf-
# ARM64 Linux cross:
#   make ARCH=aarch64 CROSS=aarch64-linux-gnu-
# Windows cross (with mingw-w64):
#   make CROSS=x86_64-w64-mingw32- EXE=.exe

CROSS ?=
CC := $(CROSS)gcc
STRIP := $(CROSS)strip
EXE ?=

CFLAGS := -O2 -Wall -Wextra -std=c99 -D_POSIX_C_SOURCE=200809L
LDFLAGS := -lm
# static link if not on macOS
UNAME_S := $(shell uname -s)
ifneq ($(UNAME_S),Darwin)
LDFLAGS += -static
endif

SRC := wav.c resample.c fec.c amodem.c main.c
OBJ := $(SRC:.c=.o)
BIN := decode$(EXE)

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDFLAGS)
	-$(STRIP) $@ 2>/dev/null || true
	@ls -la $@

%.o: %.c decode.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJ) $(BIN) $(BIN).exe

.PHONY: all clean
