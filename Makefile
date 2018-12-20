libdrm_CFLAGS := $(shell pkg-config --cflags libdrm libdrm_tegra)
libdrm_LIBS := $(shell pkg-config --libs libdrm libdrm_tegra)

libav_CFLAGS := $(shell pkg-config --cflags libavformat libavcodec libavutil)
libav_LIBS := $(shell pkg-config --libs libavformat libavcodec libavutil)

CC = $(CROSS_COMPILE)gcc
CFLAGS = -O2 -g -Wall -Werror $(EXTRA_CFLAGS) $(libdrm_CFLAGS) $(libav_CFLAGS)
LDFLAGS = $(EXTRA_LDFLAGS)
LIBS = $(libdrm_LIBS) $(libav_LIBS)

OBJS = bitstream.o drm-utils.o h264-parser.o image.o utils.o vde-decode.o

vde-decode: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

$(OBJS): %.o: %.c
	$(CC) $(CFLAGS) -o $@ -c $<

clean:
	rm -f vde-decode $(OBJS)
