CC ?= gcc
CFLAGS ?= -O2
override CFLAGS += -Wall -Wextra -Wno-unused-parameter
all: bananad banana-panel-tune
bananad: bananad.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<
banana-panel-tune: banana-panel-tune.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< -lm
install: all
	install -D -m 755 bananad $(DESTDIR)/usr/sbin/bananad
	install -D -m 755 banana-panel-tune $(DESTDIR)/usr/sbin/banana-panel-tune
clean:
	rm -f bananad banana-panel-tune
.PHONY: all install clean
