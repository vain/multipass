LDLIBS += -lX11 -lXft
CFLAGS += -std=c99 -Wall -Wextra -I/usr/include/freetype2

.PHONY: all clean

all: multipass

multipass: multipass.c config.h

config.h:
	cp -v config.def.h config.h

clean:
	rm multipass
