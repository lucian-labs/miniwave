CC = gcc
CFLAGS = -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter
LDFLAGS = -lasound -ljack -lm -lpthread -lrt

TARGET = miniwave
SRC = miniwave.c

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC) instruments.h fm-synth.h presets.h ym2413.h
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS)

clean:
	rm -f $(TARGET)
