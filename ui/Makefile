# banana-ui. Cross builds set CXX (and PKG_CONFIG for the target's sysroot); Buildroot does both.
#   make                 the frontend (needs libdrm, freetype; ALSA=1 by default for the menu sounds)
#   make host            a headless build for this machine (no ALSA), for tests and screenshots
CXX ?= g++
PKG_CONFIG ?= pkg-config
ALSA ?= 1
CXXFLAGS ?= -O2
override CXXFLAGS += -std=c++17 -Wall -Wno-unused-function
override LDLIBS += -lpthread

SRC = src/main.cpp src/soft.cpp src/kms.cpp src/input.cpp src/library.cpp src/sound.cpp src/splash.cpp src/settings.cpp src/wifi.cpp src/zip.cpp src/carts.cpp src/cart3d.cpp rnds/RndsRaster.cpp rnds/RndsUI.cpp
PKGS = freetype2 libdrm zlib
ifeq ($(ALSA),1)
PKGS += alsa
override CXXFLAGS += -DBANANA_ALSA
endif
override CXXFLAGS += $(shell $(PKG_CONFIG) --cflags $(PKGS))
override LDLIBS += $(shell $(PKG_CONFIG) --libs $(PKGS))

banana-ui: $(SRC) $(wildcard src/*.h rnds/*.h)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $(SRC) $(LDLIBS)

# box art, title screens, genres and years from libretro's free collections (Settings and the first-time setup)
banana-scrape: src/scrape.cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $< -lpthread -ldl

# the game panel's DRM lease for RetroArch (preloaded by run-retroarch)
CC ?= gcc
banana-leaseshim.so: src/leaseshim.c
	$(CC) $(CFLAGS) $(LDFLAGS) -O2 -Wall -shared -fPIC -o $@ $< -ldl

host:
	$(MAKE) ALSA=0 banana-ui

install:
	install -D -m 755 banana-ui $(DESTDIR)/usr/bin/banana-ui
	install -D -m 755 banana-leaseshim.so $(DESTDIR)/usr/lib/banana/banana-leaseshim.so
	install -D -m 755 banana-scrape $(DESTDIR)/usr/bin/banana-scrape
	# (the folders' contents: "cp -r dir dst" into a dst that is there from a build before nests a copy in it, and
	# the files in use were never replaced)
	mkdir -p $(DESTDIR)/usr/share/banana/rnds $(DESTDIR)/usr/share/banana/splash $(DESTDIR)/usr/share/banana/banana
	cp -r assets/rnds/. $(DESTDIR)/usr/share/banana/rnds/
	cp -r assets/splash/. $(DESTDIR)/usr/share/banana/splash/
	cp -r assets/banana/. $(DESTDIR)/usr/share/banana/banana/	# (fonts/: Inter, the Carts theme's)

clean:
	rm -f banana-ui banana-leaseshim.so banana-scrape

.PHONY: host install clean
