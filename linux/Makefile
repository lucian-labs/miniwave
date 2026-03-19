CC = gcc
CFLAGS = -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter

# Link against PipeWire's JACK library so we connect to PipeWire natively
PW_JACK_DIR = /usr/lib/x86_64-linux-gnu/pipewire-0.3/jack
ifneq ($(wildcard $(PW_JACK_DIR)/libjack.so),)
  JACK_LDFLAGS = -L$(PW_JACK_DIR) -Wl,-rpath,$(PW_JACK_DIR) -ljack
else
  JACK_LDFLAGS = -ljack
endif

LDFLAGS = -lasound $(JACK_LDFLAGS) -lm -lpthread -lrt

TARGET = miniwave
SRC = miniwave.c

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC) instruments.h fm-synth.h presets.h ym2413.h
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS)

clean:
	rm -f $(TARGET)
