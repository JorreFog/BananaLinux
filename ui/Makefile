# banana-ui. Cross builds set CXX (and PKG_CONFIG for the target's sysroot); Buildroot does both.
#   make                 the frontend (needs libdrm, freetype; ALSA=1 by default for the menu sounds)
#   make host            a headless build for this machine (no ALSA), for tests and screenshots
CXX ?= g++
PKG_CONFIG ?= pkg-config
ALSA ?= 1
CXXFLAGS ?= -O2
override CXXFLAGS += -std=c++17 -Wall -Wno-unused-function
override LDLIBS += -lpthread

SRC = src/main.cpp src/soft.cpp src/kms.cpp src/input.cpp src/library.cpp src/sound.cpp src/splash.cpp src/settings.cpp rnds/RndsRaster.cpp rnds/RndsUI.cpp
PKGS = freetype2 libdrm
ifeq ($(ALSA),1)
PKGS += alsa
override CXXFLAGS += -DBANANA_ALSA
endif
override CXXFLAGS += $(shell $(PKG_CONFIG) --cflags $(PKGS))
override LDLIBS += $(shell $(PKG_CONFIG) --libs $(PKGS))

banana-ui: $(SRC) $(wildcard src/*.h rnds/*.h)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $(SRC) $(LDLIBS)

host:
	$(MAKE) ALSA=0 banana-ui

install: banana-ui
	install -D -m 755 banana-ui $(DESTDIR)/usr/bin/banana-ui
	mkdir -p $(DESTDIR)/usr/share/banana
	cp -r assets/rnds $(DESTDIR)/usr/share/banana/rnds
	cp -r assets/splash $(DESTDIR)/usr/share/banana/splash
	cp -r assets/banana $(DESTDIR)/usr/share/banana/banana

clean:
	rm -f banana-ui

.PHONY: host install clean
