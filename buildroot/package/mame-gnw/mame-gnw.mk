################################################################################
#
# mame-gnw
#
################################################################################

# MAME's 0.289 release, only the hh_sm510 driver (176 systems: all of Nintendo's Game & Watch); BananaOS's patches in
# patches/mame-gnw
MAME_GNW_VERSION = mame0289
MAME_GNW_SITE = $(call github,mamedev,mame,$(MAME_GNW_VERSION))
MAME_GNW_LICENSE = GPL-2.0+
MAME_GNW_LICENSE_FILES = COPYING
MAME_GNW_DEPENDENCIES = host-python3 sdl2 sdl2_ttf

# MAME's own makefile, cross-compiled with Buildroot's GCC (its build tools, genie, with the host's): SDL2 from the
# sysroot, no X11, no desktop OpenGL (SDL's GLES renderer draws), no Qt debugger, no fontconfig (patch 0001), no MIDI
# or extra sound back ends (SDL's ALSA). USE_OZONE: bgfx's EGL headers without X11.
MAME_GNW_MAKE_OPTS = \
	SUBTARGET=gnw SOURCES=src/mame/handheld/hh_sm510.cpp \
	TARGETOS=linux CROSS_BUILD=1 PLATFORM=arm64 \
	OVERRIDE_CC="$(TARGET_CC)" OVERRIDE_CXX="$(TARGET_CXX)" OVERRIDE_AR="$(TARGET_AR)" \
	SDL_INSTALL_ROOT=$(STAGING_DIR)/usr PYTHON_EXECUTABLE=$(HOST_DIR)/bin/python3 \
	NO_X11=1 NO_USE_XINPUT=1 NO_OPENGL=1 USE_QTDEBUG=0 NO_USE_MIDI=1 NO_USE_PORTAUDIO=1 \
	NO_USE_PULSEAUDIO=1 NO_USE_PIPEWIRE=1 TOOLS=0 NOWERROR=1 OPTIMIZE=2 VERBOSE=0 \
	ARCHOPTS="-DMAME_NO_FONTCONFIG -DUSE_OZONE"

# at most 6 compilers at a time: some of MAME's files take 1-2 GB each to compile, and at 16 a 32 GB machine ran out
# of memory (its OOM killer took the desktop with it)
MAME_GNW_JOBS = $(shell n=$(PARALLEL_JOBS); [ $$n -gt 6 ] && n=6; echo $$n)

define MAME_GNW_BUILD_CMDS
	$(TARGET_MAKE_ENV) PKG_CONFIG="$(PKG_CONFIG_HOST_BINARY)" $(MAKE) -j$(MAME_GNW_JOBS) -C $(@D) $(MAME_GNW_MAKE_OPTS)
	$(TARGET_CC) $(TARGET_CFLAGS) -O2 -o $(@D)/gw-hotkeys $(MAME_GNW_PKGDIR)/gw-hotkeys.c
endef

# the emulator, and the helper run-gw starts beside it for the F button (F + Start quits, F alone is the menu)
define MAME_GNW_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/gnw $(TARGET_DIR)/usr/bin/mame-gnw
	$(INSTALL) -D -m 0755 $(@D)/gw-hotkeys $(TARGET_DIR)/usr/libexec/banana/gw-hotkeys
endef

$(eval $(generic-package))
