CC = gcc
CFLAGS = -m32 -O2 -Wall -Wextra -std=c11 -pthread

SRCS = uci.c board.c movegen.c eval.c tt.c book.c tb.c search.c tables.c bitboard.c
OBJS = $(SRCS:.c=.o)
TARGET = mchess8_uci

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

%.o: %.c mchess8.h tables.h bitboard.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET) mchess8_uci.exe

win32: $(SRCS) mchess8.h tables.h bitboard.h
	zig cc -target x86-windows-gnu -O2 -Wall -Wextra -std=c11 -o mchess8_uci.exe $(SRCS)

macos: $(SRCS) mchess8.h tables.h bitboard.h
	zig cc -target x86_64-macos -O2 -Wall -Wextra -std=c11 -pthread -o mchess8_uci_mac_intel $(SRCS)
	zig cc -target aarch64-macos -O2 -Wall -Wextra -std=c11 -pthread -o mchess8_uci_mac_arm64 $(SRCS)

win64: $(SRCS) mchess8.h tables.h bitboard.h
	zig cc -target x86_64-windows-gnu -O2 -Wall -Wextra -std=c11 -o mchess8_uci_x64.exe $(SRCS)

linux64: $(SRCS) mchess8.h tables.h bitboard.h
	gcc -m64 -O2 -Wall -Wextra -std=c11 -pthread -o mchess8_uci_x64 $(SRCS)
