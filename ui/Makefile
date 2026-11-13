# banana-ui. Cross builds set CXX (and PKG_CONFIG for the target's sysroot); Buildroot does both.
#   make                 the frontend (needs libdrm, freetype; ALSA=1 by default for the menu sounds)
#   make host            a headless build for this machine (no ALSA), for tests and screenshots
CXX ?= g++
PKG_CONFIG ?= pkg-config
ALSA ?= 1
CXXFLAGS ?= -O2
override CXXFLAGS += -std=c++17 -Wall -Wno-unused-function
override LDLIBS += -lpthread

SRC = src/main.cpp src/recorder.cpp src/lang.cpp src/soft.cpp src/kms.cpp src/colors.cpp src/input.cpp src/library.cpp src/sound.cpp src/splash.cpp src/arrival.cpp src/settings.cpp src/speaker_eq.cpp src/settings_pal.cpp src/settings_defaults.cpp src/wifi.cpp src/bluetooth.cpp src/zip.cpp src/zipdir.cpp src/carts.cpp src/cart3d.cpp src/theme.cpp src/pokemon.cpp src/pokemap.cpp src/gamecomp.cpp src/vkbd.cpp src/json.cpp src/expr.cpp src/layout.cpp src/fileops.cpp src/settings_files.cpp src/complink.cpp src/bcomp.c rnds/RndsRaster.cpp rnds/RndsUI.cpp src/settings_gamepanel.cpp src/gamesync.cpp
# (the DS Pokémon companion: the overlay BananaDS shows, its pages, the ROM reader, the guide data)
SRC += src/bovl.c src/nds/overlay.cpp src/nds/pages.cpp src/nds/model.cpp src/nds/nitro.cpp src/nds/rom.cpp src/nds/townmap.cpp src/nds/guide.cpp src/nds/ram.cpp src/nds/logic.cpp src/nds/session.cpp src/nds/romfile.cpp src/gba/gen3live.cpp src/gba/host.cpp src/gba/panel.cpp src/gba/gen3rom.cpp src/gba/hacks.cpp src/gba/gen3map.cpp src/gba/expansion.cpp
# (Bshare's pages: its helper, banana-bshare, does the network)
SRC += src/settings_bshare.cpp
# (Home server's pairing pages, and the QR code library they draw RomM's approval page with)
SRC += src/settings_homesrv.cpp src/qr/qrcodegen.cpp src/get_list.cpp
# (Files' servers: their pages, and the bits of banana-fetch they share with it; no network in banana-ui itself)
SRC += src/settings_servers.cpp src/nas.cpp src/sysfolders.cpp src/fetch/names.cpp src/fetch/jobfile.cpp src/fetch/servers.cpp
PKGS = freetype2 libdrm zlib
UI_FLAGS = -Isrc
ifeq ($(ALSA),1)
PKGS += alsa
UI_FLAGS += -DBANANA_ALSA
endif
# (banana-ui's own: asked for when it is built, so its helpers need none of its libraries)
UI_FLAGS += $(shell $(PKG_CONFIG) --cflags $(PKGS))
UI_LIBS = $(shell $(PKG_CONFIG) --libs $(PKGS))

banana-ui: $(SRC) $(wildcard src/*.h src/fetch/*.h rnds/*.h)
	$(CXX) $(CXXFLAGS) $(UI_FLAGS) $(LDFLAGS) -o $@ $(SRC) $(UI_LIBS) $(LDLIBS)

# box art, title screens, genres and years from libretro's free collections (Settings and the first-time setup)
banana-scrape: src/scrape.cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $< -lpthread -ldl

# home server sync (Settings > Network > Home server): Romhost and RomM, games, box art and saves
banana-sync: src/sync.cpp src/sync_core.cpp src/sync_romm.cpp src/save_adapt.cpp src/save_adapt.h src/save_paths.cpp src/save_paths.h src/save_vault.cpp src/save_vault.h src/sync_core.h src/sync_romm.h src/sync_util.h src/get_list.cpp src/get_list.h src/json.cpp src/json.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ src/sync.cpp src/sync_core.cpp src/sync_romm.cpp src/save_adapt.cpp src/save_paths.cpp src/save_vault.cpp src/get_list.cpp src/json.cpp -ldl

# Bshare (the shelf's tile): files to and from phones and computers with LocalSend, run by banana-ui while Receive
# is on or its page is open (settings_bshare.cpp)
banana-bshare: src/bshare.cpp src/json.cpp src/json.h
	$(CXX) $(CXXFLAGS) $(shell $(PKG_CONFIG) --cflags libcurl libcrypto) $(LDFLAGS) -o $@ src/bshare.cpp src/json.cpp \
		$(shell $(PKG_CONFIG) --libs libcurl libcrypto) -lpthread

# Files' servers: SMB, SFTP and FTP, the downloads and the unpacking, run by banana-ui while there is work
# (settings_servers.cpp). SMB=0 builds it without libsmb2, for a computer that has none.
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

# the speaker EQ (Settings > Display & sound > Speaker EQ): an ALSA filter plugin, in alsa-lib's plugin folder where
# bananad's `type banana_eq` finds it. -DPIC: alsa-lib's plugin macros only skip their static-build symbol with it
libasound_module_pcm_banana_eq.so: src/eq/pcm_banana_eq.c src/eq/eq.h
	$(CC) $(CFLAGS) $(LDFLAGS) -O2 -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -shared -fPIC -DPIC \
		$(shell $(PKG_CONFIG) --cflags alsa) -o $@ $< $(shell $(PKG_CONFIG) --libs alsa) -lm

host:
	$(MAKE) ALSA=0 banana-ui

# the Pokémon companion's decoder on data made from the documented layouts (tests/run.sh runs it)
tests/pokemon-test: tests/pokemon-test.cpp src/pokemon.cpp src/pokemap.cpp src/pokemon.h src/zip.cpp src/zipdir.cpp
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/pokemon-test.cpp src/pokemon.cpp src/pokemap.cpp src/zip.cpp src/zipdir.cpp -lz -lpthread

# the game companion's engine (tests/run.sh runs it)
tests/gamecomp-test: tests/gamecomp-test.cpp src/gamecomp.cpp src/gamecomp.h src/json.cpp src/expr.cpp src/zip.cpp src/zipdir.cpp
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/gamecomp-test.cpp src/gamecomp.cpp src/json.cpp src/expr.cpp src/zip.cpp src/zipdir.cpp -lz -lpthread

# the translations' number placeholders (tests/run.sh runs it)
tests/lang-raw-test: tests/lang-raw-test.cpp src/lang.cpp src/lang.h
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/lang-raw-test.cpp src/lang.cpp -lpthread

# the save sync's blank saves, vault and journal (tests/run.sh runs it)
tests/paths-test: tests/paths-test.cpp src/save_paths.cpp src/save_paths.h src/sync_core.cpp src/sync_core.h src/json.cpp
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/paths-test.cpp src/save_paths.cpp src/sync_core.cpp src/json.cpp -ldl

tests/vault-test: tests/vault-test.cpp src/save_adapt.cpp src/save_adapt.h src/save_vault.cpp src/save_vault.h src/sync_core.cpp src/sync_core.h src/json.cpp
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/vault-test.cpp src/save_adapt.cpp src/save_vault.cpp src/sync_core.cpp src/json.cpp -ldl

# the menu's side of the sync around a game (tests/run.sh runs it)
tests/gamesync-test: tests/gamesync-test.cpp src/gamesync.cpp src/gamesync.h src/sync_util.h src/json.cpp
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/gamesync-test.cpp src/gamesync.cpp src/json.cpp

# the left stick's deadzone and hysteresis (tests/run.sh runs it)
tests/input-test: tests/input-test.cpp src/input.cpp src/input.h
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/input-test.cpp src/input.cpp

# the speaker EQ's arithmetic: the profile parser, the filters against reference curves, headroom, stability (tests/run.sh runs it)
tests/eq-test: tests/eq-test.c src/eq/eq.h
	$(CC) $(CFLAGS) -O2 -Wall -Wextra -Wno-unused-function -Isrc -o $@ tests/eq-test.c -lm

# the menu's WAV reader on theme sounds (tests/run.sh runs it)
tests/wav-test: tests/wav-test.cpp src/sound.cpp src/sound.h
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/wav-test.cpp -lpthread

# the menu's language lookup on the shipped translations (tests/run.sh runs it)
tests/lang-test: tests/lang-test.cpp src/lang.cpp src/lang.h
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/lang-test.cpp src/lang.cpp -lpthread

install:
	install -D -m 755 banana-ui $(DESTDIR)/usr/bin/banana-ui
	install -D -m 755 banana-leaseshim.so $(DESTDIR)/usr/lib/banana/banana-leaseshim.so
	install -D -m 755 libasound_module_pcm_banana_eq.so $(DESTDIR)/usr/lib/alsa-lib/libasound_module_pcm_banana_eq.so
	# (the speaker EQ's profiles measured for a handheld: <board>.txt, /etc/banana/board's name)
	mkdir -p $(DESTDIR)/usr/share/banana/speaker-eq
	cp -r assets/speaker-eq/. $(DESTDIR)/usr/share/banana/speaker-eq/
	install -D -m 755 banana-scrape $(DESTDIR)/usr/bin/banana-scrape
	install -D -m 755 banana-sync $(DESTDIR)/usr/bin/banana-sync
	install -D -m 755 banana-bshare $(DESTDIR)/usr/bin/banana-bshare
	install -D -m 755 banana-fetch $(DESTDIR)/usr/bin/banana-fetch
	# (Bshare's tile on the shelf: its one entry, systems.conf's [bshare])
	mkdir -p $(DESTDIR)/usr/share/banana/apps/bshare
	touch $(DESTDIR)/usr/share/banana/apps/bshare/Bshare.app
	# (the folders' contents: "cp -r dir dst" into a dst that is there from a build before nests a copy in it, and
	# the files in use were never replaced)
	mkdir -p $(DESTDIR)/usr/share/banana/rnds $(DESTDIR)/usr/share/banana/splash $(DESTDIR)/usr/share/banana/banana
	cp -r assets/rnds/. $(DESTDIR)/usr/share/banana/rnds/
	cp -r assets/splash/. $(DESTDIR)/usr/share/banana/splash/
	cp -r assets/banana/. $(DESTDIR)/usr/share/banana/banana/	# (fonts/: Inter, the Carts theme's)
	# the menu's languages (lang.h): one file each, the menu's and the DS emulators' translations
	mkdir -p $(DESTDIR)/usr/share/banana/lang
	cp -r assets/lang/. $(DESTDIR)/usr/share/banana/lang/
	# the shelf's Files tile: one of the menu's own pages (systems.conf's [files]), its one entry
	mkdir -p $(DESTDIR)/usr/share/banana/apps/files
	touch $(DESTDIR)/usr/share/banana/apps/files/Files.app

clean:
	rm -f banana-ui banana-leaseshim.so libasound_module_pcm_banana_eq.so banana-scrape banana-sync banana-bshare banana-fetch tests/pokemon-test tests/gamecomp-test tests/input-test tests/vault-test tests/wav-test tests/eq-test tests/lang-raw-test tests/lang-test tests/complink-test tests/nds-pages-test tests/nds-rom-test tests/nds-rom-dump tests/nds-ram-test tests/nds-logic-test tests/nds-romfile-test tests/nds-townmap-test tests/gba-live-test tests/gba-pages-test tests/gba-rom-test tests/gba-items-gen tests/soft-base-test tests/gba-exp-test tests/gba-expitems-gen tests/gba-hidden-items tests/paths-test tests/gamesync-test

.PHONY: host install clean

# the companion's shared-memory link (tests/run.sh runs it)
tests/complink-test: tests/complink-test.cpp src/complink.cpp src/complink.h src/bcomp.c src/bcomp.h
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/complink-test.cpp src/complink.cpp -x c++ src/bcomp.c

# the DS Pokémon companion's ROM reader: its containers and decoders on data made from the documented layouts, and on
# the player's own ROMs when given a folder of them (tests/run.sh runs the first part)
NDS_ROM_SRC = src/nds/nitro.cpp src/nds/rom.cpp src/nds/townmap.cpp src/nds/model.cpp
tests/nds-rom-test: tests/nds-rom-test.cpp $(NDS_ROM_SRC) $(wildcard src/nds/*.h)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/nds-rom-test.cpp $(NDS_ROM_SRC) -lpthread
# (what the reader makes of a ROM, printed: for looking at by eye; never commit its output, it is the game's data)
tests/nds-rom-dump: tests/nds-rom-dump.cpp $(NDS_ROM_SRC) $(wildcard src/nds/*.h)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/nds-rom-dump.cpp $(NDS_ROM_SRC) -lpthread
# the DS Pokémon companion's ROM file: a game's header from inside a .zip, the wait for run-bananads' unpacked copy
# the DS companion's region maps (tests/run.sh runs it; give it the ROMs folder, or NDS_ROMS, for the games' own maps)
tests/nds-townmap-test: tests/nds-townmap-test.cpp $(NDS_ROM_SRC) $(wildcard src/nds/*.h)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/nds-townmap-test.cpp $(NDS_ROM_SRC) -lpthread

tests/nds-romfile-test: tests/nds-romfile-test.cpp src/nds/romfile.cpp src/nds/romfile.h src/zip.cpp src/zipdir.cpp
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/nds-romfile-test.cpp src/nds/romfile.cpp src/zip.cpp src/zipdir.cpp -lz -lpthread
# the DS Pokémon companion's guide loader (tests/run.sh runs it)
# the GBA Pokémon games' ROM data for the DS companion (src/gba/gen3rom.*): tests/gba-rom-test [roms dir [png dir]]
# (or GBA_ROMS=<dir>: the player's own ROMs; without them the header checks only). tests/gba-items-gen remakes
# src/gba/gen3items.inc from an Emerald, a FireRed and a HeartGold ROM
GBA_ROM_SRC = src/gba/gen3rom.cpp src/gba/gen3live.cpp src/gba/hacks.cpp src/gba/expansion.cpp src/gba/gen3map.cpp src/pokemon.cpp src/pokemap.cpp src/zip.cpp src/zipdir.cpp src/nds/nitro.cpp src/nds/model.cpp src/nds/guide.cpp src/json.cpp
tests/gba-rom-test: tests/gba-rom-test.cpp $(GBA_ROM_SRC) src/gba/gen3rom.h src/gba/gen3live.h src/gba/hacks.h src/gba/gen3items.inc src/gba/gen4names.inc src/pokemon.h src/nds/model.h
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/gba-rom-test.cpp $(GBA_ROM_SRC) -lz -lpthread

# the hidden items of a GBA Pokémon ROM, for its guide's secrets: tests/gba-hidden-items <rom.gba>
tests/gba-hidden-items: tests/gba-hidden-items.cpp src/pokemon.cpp src/pokemap.cpp src/pokemon.h src/zip.cpp src/zipdir.cpp
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/gba-hidden-items.cpp src/pokemon.cpp src/pokemap.cpp src/zip.cpp src/zipdir.cpp -lz -lpthread

tests/gba-items-gen: tests/gba-items-gen.cpp $(NDS_ROM_SRC) src/pokemon.cpp src/pokemap.cpp src/zip.cpp src/zipdir.cpp
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/gba-items-gen.cpp $(NDS_ROM_SRC) src/pokemon.cpp src/pokemap.cpp src/zip.cpp src/zipdir.cpp -lz -lpthread

# the pokeemerald-expansion ROM hacks (src/gba/expansion.*): tests/gba-exp-test [hacks dir] (skips the ROMs it doesn't
# find); tests/gba-expitems-gen remakes src/gba/expitems.inc from an Emerald ROM
tests/gba-exp-test: tests/gba-exp-test.cpp $(GBA_ROM_SRC) src/gba/expansion.h src/gba/expitems.inc src/pokemon.h src/nds/model.h
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/gba-exp-test.cpp $(GBA_ROM_SRC) -lz -lpthread
tests/gba-expitems-gen: tests/gba-expitems-gen.cpp $(GBA_ROM_SRC)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/gba-expitems-gen.cpp $(GBA_ROM_SRC) -lz -lpthread

tests/guide-test: tests/guide-test.cpp src/nds/guide.cpp src/nds/guide.h src/nds/model.h src/json.cpp
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/guide-test.cpp src/nds/guide.cpp src/json.cpp
# the DS Pokémon companion's pages on made-up games: every page as a PNG at both sizes, and the touches (tests/run.sh runs it)
# the DS companion's pages on the five GBA games' ROMs (a look: tests/out/gba-pages; not in run.sh - needs the ROMs)
GBA_PAGES_SRC = tests/gba-pages-test.cpp src/gba/gen3rom.cpp src/gba/hacks.cpp src/gba/expansion.cpp src/gba/gen3map.cpp src/nds/nitro.cpp src/gba/gen3live.cpp src/pokemon.cpp src/pokemap.cpp src/zip.cpp src/zipdir.cpp src/nds/pages.cpp src/nds/model.cpp src/nds/logic.cpp src/nds/guide.cpp src/json.cpp src/settings_pal.cpp src/soft.cpp src/lang.cpp src/theme.cpp rnds/RndsRaster.cpp rnds/RndsUI.cpp
tests/gba-pages-test: $(GBA_PAGES_SRC) src/nds/pages.h src/nds/model.h src/gba/gen3rom.h src/gba/gen3live.h
	$(CXX) $(CXXFLAGS) -Isrc $(shell $(PKG_CONFIG) --cflags freetype2) -o $@ $(GBA_PAGES_SRC) $(shell $(PKG_CONFIG) --libs freetype2 zlib) -lpthread

NDS_PAGES_SRC = tests/nds-pages-test.cpp src/nds/pages.cpp src/nds/logic.cpp src/nds/model.cpp src/settings_pal.cpp src/soft.cpp src/lang.cpp src/theme.cpp rnds/RndsRaster.cpp rnds/RndsUI.cpp
tests/nds-pages-test: $(NDS_PAGES_SRC) tests/nds-fixture.h src/nds/pages.h src/nds/model.h
	$(CXX) $(CXXFLAGS) -Isrc $(shell $(PKG_CONFIG) --cflags freetype2) -o $@ $(NDS_PAGES_SRC) $(shell $(PKG_CONFIG) --libs freetype2 zlib) -lpthread

# the DS Pokémon companion's RAM reader on synthetic RAM (tests/run.sh runs it; give it ~/nds-companion/ram-kit/out
# to check the snapshots recorded on the handhelds too)
NDS_RAM_SRC = src/nds/ram.cpp src/nds/model.cpp src/nds/nitro.cpp
tests/nds-ram-test: tests/nds-ram-test.cpp $(NDS_RAM_SRC) $(wildcard src/nds/*.h)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/nds-ram-test.cpp $(NDS_RAM_SRC)

# the GBA games' state as the DS companion's (tests/run.sh runs it)
tests/gba-live-test: tests/gba-live-test.cpp src/gba/gen3live.cpp src/gba/gen3live.h src/pokemon.cpp src/pokemon.h src/pokemap.cpp src/nds/model.h
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/gba-live-test.cpp src/gba/gen3live.cpp src/pokemon.cpp src/pokemap.cpp src/zip.cpp src/zipdir.cpp -lz -lpthread

# the software backend's background copy after invalidate (tests/run.sh runs it)
tests/soft-base-test: tests/soft-base-test.cpp src/soft.cpp src/soft.h
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/soft-base-test.cpp src/soft.cpp -lpthread

# the DS Pokémon companion's advice (damage, catching, team score, guide progress; tests/run.sh runs it)
tests/nds-logic-test: tests/nds-logic-test.cpp src/nds/logic.cpp src/nds/session.cpp $(NDS_RAM_SRC) $(wildcard src/nds/*.h)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/nds-logic-test.cpp src/nds/logic.cpp src/nds/session.cpp $(NDS_RAM_SRC)
