################################################################################
#
# scummvm
#
################################################################################

# ScummVM standalone (SDL 2), on Wayland. Ported from ROCKNIX's scummvm-sa (packages/emulators/standalone/scummvm-sa at
# 6cf94dba, GPL-2.0): the same release (2026.1.0) and its configure options, with its SM8550 choice of no OpenGL
# (the software renderer, scaled by the compositor) and its settings (scummvm.ini). Not ROCKNIX's: no FluidSynth or
# its General MIDI sound font (a large download; MIDI music plays through ScummVM's own AdLib emulation instead), and
# ScummVM's sound through SDL to ALSA as ROCKNIX's (--disable-alsa only drops the ALSA MIDI sequencer).
# The libretro ScummVM core isn't used: its build clones its dependencies at build time.
SCUMMVM_VERSION = 2026.1.0
SCUMMVM_SITE = $(call github,scummvm,scummvm,v$(SCUMMVM_VERSION))
SCUMMVM_LICENSE = GPL-3.0+
SCUMMVM_LICENSE_FILES = COPYING
SCUMMVM_DEPENDENCIES = host-pkgconf sdl2 sdl2_net freetype zlib libpng jpeg libogg libvorbis flac libmad
SCUMMVM_CONF_OPTS = --host=$(GNU_TARGET_NAME) --prefix=/usr --backend=sdl --disable-alsa \
	--with-sdl-prefix=$(STAGING_DIR)/usr/bin --disable-debug --enable-release --enable-vkeybd --enable-optimizations \
	--disable-opengl-game --disable-opengl-game-classic --disable-opengl-game-shaders --opengl-mode=none \
	--disable-fluidsynth

# ROCKNIX's post_unpack: sdl2-config, not sdl-config
define SCUMMVM_SDL2_CONFIG
	$(SED) 's|sdl-config|sdl2-config|g' $(@D)/configure
endef
SCUMMVM_POST_PATCH_HOOKS += SCUMMVM_SDL2_CONFIG

define SCUMMVM_CONFIGURE_CMDS
	cd $(@D) && $(TARGET_CONFIGURE_OPTS) PKG_CONFIG="$(PKG_CONFIG_HOST_BINARY)" ./configure $(SCUMMVM_CONF_OPTS)
endef

define SCUMMVM_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D)
endef

# ScummVM, its themes and engine data (/usr/share/scummvm, from its own install), and ROCKNIX's settings as the
# template run-scummvm copies to /data/scummvm (/usr/share/scummvm itself, beside ScummVM's files)
define SCUMMVM_INSTALL_TARGET_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) DESTDIR=$(TARGET_DIR) install
	rm -rf $(TARGET_DIR)/usr/share/applications $(TARGET_DIR)/usr/share/icons $(TARGET_DIR)/usr/share/metainfo \
		$(TARGET_DIR)/usr/share/pixmaps $(TARGET_DIR)/usr/share/doc/scummvm
	$(call THOR_EMULATORS_INSTALL_TEMPLATES,scummvm)
endef

$(eval $(generic-package))
