################################################################################
#
# bananads: Banana's own DS emulator, when it is ready (docs/EMULATORS.md). Fill in the three lines marked
# FILL IN; the rest is the convention the other packages follow.
#
################################################################################

BANANADS_VERSION = main	# FILL IN: the commit to build (pinned, as superdrastic's is)
BANANADS_SITE = https://github.com/JorreFog/BananaDS.git
BANANADS_SITE_METHOD = git
BANANADS_LICENSE = MIT
BANANADS_LICENSE_FILES = LICENSE
# libdrm (the panels), ALSA (sound), zlib (zipped ROMs); EGL/GLES come from mesa3d-panfrost if it draws with the GPU
BANANADS_DEPENDENCIES = libdrm alsa-lib zlib

# FILL IN: its build. A Makefile honouring CC/CXX/CFLAGS is the simplest; cmake or meson packages are
# $(eval $(cmake-package)) / $(eval $(meson-package)) with BANANADS_CONF_OPTS instead of these two.
define BANANADS_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" CFLAGS="$(TARGET_CFLAGS) -O2" \
		CXXFLAGS="$(TARGET_CXXFLAGS) -O2" LDFLAGS="$(TARGET_LDFLAGS)"
endef

# FILL IN: what it installs. The launcher expects the program at /usr/bin/bananads; anything else it needs at run
# time (shaders, fonts, a BIOS it carries) under /usr/share/bananads.
define BANANADS_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/bananads $(TARGET_DIR)/usr/bin/bananads
	mkdir -p $(TARGET_DIR)/usr/share/bananads
endef

$(eval $(generic-package))
