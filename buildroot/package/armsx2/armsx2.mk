################################################################################
#
# armsx2
#
################################################################################

# ARMSX2, PCSX2 with its EE/IOP/VU recompilers ported to ARM64 (the PS2 emulator ROCKNIX ships on the SM8550), at
# ROCKNIX's commit (packages/emulators/standalone/armsx2-sa at 6cf94dba, GPL-2.0) with its one patch (0001: page and
# cache-line sizes when cross-compiling). Not ROCKNIX's build: ROCKNIX builds the Qt front end with clang and lld; this
# is ARMSX2's own SDL 3 front end (armsx2-sdl, ENABLE_SDL_FRONTEND: no Qt, quits on SIGTERM, runs on a Wayland
# compositor or straight on KMS) with Buildroot's GCC, which ARMSX2's CMake accepts. Untested on the Thor.
# ROCKNIX also ships ARMSX2's own Turnip build for the SM8550; here the system's Mesa Turnip is used.
ARMSX2_VERSION = ce3743fa039f9aea4c53e4f2fd7d200043dc86ab
ARMSX2_SITE = $(call github,ARMSX2,ARMSX2,$(ARMSX2_VERSION))
ARMSX2_LICENSE = GPL-3.0+, LGPL-3.0+
ARMSX2_LICENSE_FILES = COPYING.GPLv3
ARMSX2_SUPPORTS_IN_SOURCE_BUILD = NO
ARMSX2_DEPENDENCIES = host-pkgconf sdl3 libpng jpeg zlib zstd lz4 webp freetype fontconfig plutosvg plutovg \
	libcurl libpcap shaderc dbus extra-cmake-modules wayland libegl libgles vulkan-headers \
	$(if $(BR2_PACKAGE_HAS_UDEV),udev)
ARMSX2_CONF_OPTS = \
	-DCMAKE_BUILD_TYPE=Release -DARMSX2_VERSION=2.9.1 -DPACKAGE_MODE=ON \
	-DENABLE_QT_UI=OFF -DENABLE_QT_DEBUGGER=OFF -DENABLE_SDL_FRONTEND=ON -DENABLE_LIBRETRO=OFF \
	-DENABLE_TESTS=OFF -DENABLE_GSRUNNER=OFF \
	-DUSE_VULKAN=ON -DUSE_OPENGL=ON -DUSE_GLES=ON -DX11_API=OFF -DWAYLAND_API=ON -DUSE_BACKTRACE=OFF \
	-DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF -DLTO_PCSX2_CORE=ON -DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON

# PACKAGE_MODE installs armsx2-sdl in /usr/bin and its resources (game database, shaders, fonts) in /usr/share/PCSX2;
# then ROCKNIX's settings (PCSX2.ini: Vulkan, the pad, the BIOS, memory card and state folders as BananaOS has them)
# as /usr/share/armsx2. And ARMSX2's game fixes for mobile GPUs (Adreno among them), which it reads from its resources
# folder when they are there: its build copies that file beside the built program only, PACKAGE_MODE doesn't install it
define ARMSX2_INSTALL_BANANA
	$(call THOR_EMULATORS_INSTALL_TEMPLATES,armsx2)
	$(INSTALL) -D -m 0644 $(@D)/bin/resources-overlay/armsx2_overrides.yaml \
		$(TARGET_DIR)/usr/share/PCSX2/resources/armsx2_overrides.yaml
endef
ARMSX2_POST_INSTALL_TARGET_HOOKS += ARMSX2_INSTALL_BANANA

$(eval $(cmake-package))
