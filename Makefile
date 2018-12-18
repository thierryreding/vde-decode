libdrm_CFLAGS := $(shell pkg-config --cflags libdrm)
libdrm_LIBS := $(shell pkg-config --libs libdrm)

libav_CFLAGS := $(shell pkg-config --cflags libavformat libavcodec libavutil)
libav_LIBS := $(shell pkg-config --libs libavformat libavcodec libavutil)

CC = $(CROSS_COMPILE)gcc
CFLAGS = -O2 -g -Wall -Werror $(EXTRA_CFLAGS) $(libdrm_CFLAGS) $(libav_CFLAGS)
LIBS = $(libdrm_LIBS) $(libav_LIBS)

vde-decode: vde-decode.c
	$(CC) $(CFLAGS) -o $@ $< $(LIBS)

clean:
	rm -f vde-decode
