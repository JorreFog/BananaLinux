################################################################################
#
# armsx2
#
################################################################################

# ARMSX2, PCSX2 with its EE/IOP/VU recompilers ported to ARM64 (the PS2 emulator ROCKNIX ships on the SM8550), at
# ROCKNIX's commit (packages/emulators/standalone/armsx2-sa at 6cf94dba, GPL-2.0) with its one patch (0001: page and
# cache-line sizes when cross-compiling). Not ROCKNIX's build: ROCKNIX builds the Qt front end with clang and lld; this
# is ARMSX2's own SDL 3 front end (armsx2-sdl, ENABLE_SDL_FRONTEND: no Qt, quits on SIGTERM, runs on a Wayland
# compositor or straight on KMS), built with Buildroot's clang (below). Untested on the Thor.
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

# Built with Buildroot's clang, not GCC: ARMSX2's ARM64 recompiler calls the vtlb memory dispatchers assuming
# __attribute__((preserve_most)) (x9-x15 kept across the call), and pcsx2/vtlb.h stops the build with #error on a
# compiler without it, GCC among them (a GCC build would corrupt the EE's pinned registers on every MMIO slow path).
# lld and llvm-ar for the LTO part of the core (PCSX2_LTO: LLVM bitcode in a static library).
ARMSX2_DEPENDENCIES += host-clang host-lld
# Buildroot's clang wrapper (host/bin/clang++) runs the real compiler as clang++.br_real, a name clang doesn't read as
# C++, so it links like plain clang, without libstdc++: --driver-mode=g++ for C++. The wrapper's config file already
# gives clang the target, the sysroot and the external toolchain's GCC (libstdc++'s headers, crt files, libgcc).
# The Thor's CPU as every other package has it (GCC_TARGET_CPU, SVE off: buildroot/external.mk), and its architecture
# with SVE off too: ARMSX2's CMake adds -march=armv8.1-a when CMAKE_CXX_FLAGS has no -march, and with clang that later
# -march replaces the +nosve+nosve2 of -mcpu while cortex-a710 turns SVE back on (SVE code in 1166 functions; SIGILL on
# the Thor, whose SVE is disabled). armv9-a is the Cortex-A710's architecture.
ARMSX2_CLANG_FLAGS = -mcpu=$(GCC_TARGET_CPU) -march=armv9-a+nosve+nosve2
ARMSX2_CONF_OPTS += \
	-DCMAKE_C_COMPILER=$(HOST_DIR)/bin/clang -DCMAKE_CXX_COMPILER=$(HOST_DIR)/bin/clang++ \
	-DCMAKE_C_FLAGS="$(TARGET_CFLAGS) $(ARMSX2_CLANG_FLAGS)" \
	-DCMAKE_CXX_FLAGS="--driver-mode=g++ $(TARGET_CXXFLAGS) $(ARMSX2_CLANG_FLAGS)" \
	-DCMAKE_EXE_LINKER_FLAGS="$(TARGET_LDFLAGS) -fuse-ld=lld" \
	-DCMAKE_SHARED_LINKER_FLAGS="$(TARGET_LDFLAGS) -fuse-ld=lld" \
	-DCMAKE_AR=$(HOST_DIR)/bin/llvm-ar -DCMAKE_RANLIB=$(HOST_DIR)/bin/llvm-ranlib
# ARMSX2's own libraries (common, vixl, imgui, fmt, ... and PCSX2_LTO) static, as upstream builds them: Buildroot's
# default BUILD_SHARED_LIBS=ON made them shared libraries that aren't installed, and PCSX2_LTO, built with hidden
# visibility, exported 79 symbols as a shared library, so armsx2-sdl didn't link (PSX_INT undefined)
ARMSX2_CONF_OPTS += -DBUILD_SHARED_LIBS=OFF

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
