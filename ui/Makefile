# banana-ui. Cross builds set CXX (and PKG_CONFIG for the target's sysroot); Buildroot does both.
#   make                 the frontend (needs libdrm, freetype; ALSA=1 by default for the menu sounds)
#   make host            a headless build for this machine (no ALSA), for tests and screenshots
CXX ?= g++
PKG_CONFIG ?= pkg-config
ALSA ?= 1
CXXFLAGS ?= -O2
override CXXFLAGS += -std=c++17 -Wall -Wno-unused-function
override LDLIBS += -lpthread

SRC = src/main.cpp src/soft.cpp src/kms.cpp src/input.cpp src/library.cpp src/sound.cpp src/splash.cpp src/settings.cpp src/settings_defaults.cpp src/wifi.cpp src/zip.cpp src/zipdir.cpp src/carts.cpp src/cart3d.cpp src/theme.cpp src/json.cpp src/expr.cpp src/layout.cpp rnds/RndsRaster.cpp rnds/RndsUI.cpp
# (Downloads' pages, and the bits of banana-fetch they share with it: no network in banana-ui itself)
SRC += src/settings_downloads.cpp src/nas.cpp src/fetch/names.cpp src/fetch/jobfile.cpp src/fetch/servers.cpp
PKGS = freetype2 libdrm zlib
UI_FLAGS =
ifeq ($(ALSA),1)
PKGS += alsa
UI_FLAGS += -DBANANA_ALSA
endif
# (banana-ui's own: asked for when it is built, so banana-fetch and banana-scrape need none of its libraries)
UI_FLAGS += $(shell $(PKG_CONFIG) --cflags $(PKGS))
UI_LIBS = $(shell $(PKG_CONFIG) --libs $(PKGS))

banana-ui: $(SRC) $(wildcard src/*.h src/fetch/*.h rnds/*.h)
	$(CXX) $(CXXFLAGS) $(UI_FLAGS) $(LDFLAGS) -o $@ $(SRC) $(UI_LIBS) $(LDLIBS)

# box art, title screens, genres and years from libretro's free collections (Settings and the first-time setup)
banana-scrape: src/scrape.cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $< -lpthread -ldl

# Downloads (the shelf's tile): file servers (SMB, SFTP, FTP), the downloads and the unpacking, run by banana-ui while
# there is work (settings_downloads.cpp). SMB=0 builds it without libsmb2, for a computer that has none.
SMB ?= 1
FETCH_SRC = $(wildcard src/fetch/*.cpp) src/zipdir.cpp
FETCH_PKGS = libcurl libssh2 libssl libcrypto zlib
ifeq ($(SMB),1)
FETCH_PKGS += libsmb2
else
FETCH_SRC := $(filter-out src/fetch/remote_smb.cpp,$(FETCH_SRC))
FETCH_FLAGS = -DFETCH_NO_SMB
endif
banana-fetch: $(FETCH_SRC) $(wildcard src/fetch/*.h) src/zipdir.h
	$(CXX) $(CXXFLAGS) $(FETCH_FLAGS) $(shell $(PKG_CONFIG) --cflags $(FETCH_PKGS)) $(LDFLAGS) -o $@ $(FETCH_SRC) \
		$(shell $(PKG_CONFIG) --libs $(FETCH_PKGS)) -lpthread

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
	install -D -m 755 banana-fetch $(DESTDIR)/usr/bin/banana-fetch
	# (Downloads' tile on the shelf: its one entry, systems.conf's [downloads])
	mkdir -p $(DESTDIR)/usr/share/banana/apps/downloads
	touch $(DESTDIR)/usr/share/banana/apps/downloads/Downloads.app
	# (the folders' contents: "cp -r dir dst" into a dst that is there from a build before nests a copy in it, and
	# the files in use were never replaced)
	mkdir -p $(DESTDIR)/usr/share/banana/rnds $(DESTDIR)/usr/share/banana/splash $(DESTDIR)/usr/share/banana/banana
	cp -r assets/rnds/. $(DESTDIR)/usr/share/banana/rnds/
	cp -r assets/splash/. $(DESTDIR)/usr/share/banana/splash/
	cp -r assets/banana/. $(DESTDIR)/usr/share/banana/banana/	# (fonts/: Inter, the Carts theme's)

clean:
	rm -f banana-ui banana-leaseshim.so banana-scrape banana-fetch

.PHONY: host install clean
