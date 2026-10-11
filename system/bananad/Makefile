CC ?= gcc
CFLAGS ?= -O2
override CFLAGS += -Wall -Wextra -Wno-unused-parameter
bananad: bananad.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<
install: bananad
	install -D -m 755 bananad $(DESTDIR)/usr/sbin/bananad
clean:
	rm -f bananad
.PHONY: install clean
