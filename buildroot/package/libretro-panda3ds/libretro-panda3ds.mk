################################################################################
#
# libretro-panda3ds: Panda3DS, the Nintendo 3DS emulator, as a libretro core (run-retroarch panda3ds). Upstream at a
# pinned commit with its submodules (dynarmic, the ARM11's JIT, among them), and BananaOS's patches over it (this
# folder, applied by Buildroot): the idle loop no longer spins through the game's GPU waits, the JIT's code for the
# A55 (flags, FPSR), tiled texture decoding, batched immediate-mode draws, uniforms uploaded only when they change,
# CPU-written and render-to-texture textures. PANDA3DS_BASELINE=1 in the environment runs upstream's code paths
# instead, for comparisons. tools/3ds-perf and tools/3ds-bench measure it.
#
################################################################################

LIBRETRO_PANDA3DS_VERSION = 92e53d26b898eb89d865f8407460900a88e7c0fa
LIBRETRO_PANDA3DS_SITE = https://github.com/wheremyfoodat/Panda3DS.git
LIBRETRO_PANDA3DS_SITE_METHOD = git
LIBRETRO_PANDA3DS_GIT_SUBMODULES = YES
LIBRETRO_PANDA3DS_LICENSE = GPL-3.0
LIBRETRO_PANDA3DS_LICENSE_FILES = LICENSE
LIBRETRO_PANDA3DS_SUPPORTS_IN_SOURCE_BUILD = NO
LIBRETRO_PANDA3DS_DEPENDENCIES = libgles host-pkgconf
ifeq ($(BR2_PACKAGE_LIBRETRO_PANDA3DS_BENCH),y)
LIBRETRO_PANDA3DS_DEPENDENCIES += libegl
endif

# The core alone (no SDL or Qt window), GLES 3 through RetroArch's context, RetroArch's own audio, nothing the
# handheld doesn't use (Vulkan, Lua, Discord, RenderDoc, the HTTP server), tuned for the Cortex-A55
LIBRETRO_PANDA3DS_CONF_OPTS = \
	-DCMAKE_BUILD_TYPE=Release \
	-DBUILD_LIBRETRO_CORE=ON \
	-DUSE_LIBRETRO_AUDIO=ON \
	-DOPENGL_PROFILE=OpenGLES \
	-DENABLE_VULKAN=OFF \
	-DENABLE_LUAJIT=OFF \
	-DENABLE_DISCORD_RPC=OFF \
	-DENABLE_RENDERDOC_API=OFF \
	-DENABLE_HTTP_SERVER=OFF \
	-DENABLE_WAYLAND=OFF \
	-DENABLE_GIT_VERSIONING=OFF \
	-DENABLE_USER_BUILD=ON \
	-DBUILD_TESTING=OFF \
	-DCMAKE_C_FLAGS="$(TARGET_CFLAGS) -mcpu=cortex-a55" \
	-DCMAKE_CXX_FLAGS="$(TARGET_CXXFLAGS) -mcpu=cortex-a55"

LIBRETRO_PANDA3DS_MAKE_OPTS = panda3ds_libretro

# panda3ds-bench (BR2_PACKAGE_LIBRETRO_PANDA3DS_BENCH): the same emulator with no window, and the stress-test ROM
# (tools/3ds-bench), to measure it on the handheld itself with panfrost:
#   panda3ds-bench /usr/share/banana/3ds-bench/3ds-bench.3dsx --renderer gl
# A second build of the sources, without the libretro core's settings (its own audio, the GPU on its own thread).
ifeq ($(BR2_PACKAGE_LIBRETRO_PANDA3DS_BENCH),y)
LIBRETRO_PANDA3DS_BENCH_CONF_OPTS = $(filter-out -DBUILD_LIBRETRO_CORE=ON -DUSE_LIBRETRO_AUDIO=ON,$(LIBRETRO_PANDA3DS_CONF_OPTS)) \
	-DBUILD_BENCH=ON -DENABLE_QT_GUI=OFF
define LIBRETRO_PANDA3DS_BUILD_BENCH
	mkdir -p $(@D)/bench-build
	cd $(@D)/bench-build && $(TARGET_MAKE_ENV) $(BR2_CMAKE) $(@D) \
		-DCMAKE_TOOLCHAIN_FILE="$(HOST_DIR)/share/buildroot/toolchainfile.cmake" \
		-DCMAKE_INSTALL_PREFIX="/usr" \
		$(LIBRETRO_PANDA3DS_BENCH_CONF_OPTS)
	$(TARGET_MAKE_ENV) $(BR2_CMAKE) --build $(@D)/bench-build --target panda3ds-bench -j$(PARALLEL_JOBS)
endef
LIBRETRO_PANDA3DS_POST_BUILD_HOOKS += LIBRETRO_PANDA3DS_BUILD_BENCH
define LIBRETRO_PANDA3DS_INSTALL_BENCH
	$(INSTALL) -D -m 0755 $(@D)/bench-build/panda3ds-bench $(TARGET_DIR)/usr/bin/panda3ds-bench
	$(INSTALL) -D -m 0644 $(BR2_EXTERNAL_BANANA_PATH)/../tools/3ds-bench/3ds-bench.3dsx \
		$(TARGET_DIR)/usr/share/banana/3ds-bench/3ds-bench.3dsx
endef
LIBRETRO_PANDA3DS_POST_INSTALL_TARGET_HOOKS += LIBRETRO_PANDA3DS_INSTALL_BENCH
endif

define LIBRETRO_PANDA3DS_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(LIBRETRO_PANDA3DS_BUILDDIR)/panda3ds_libretro.so $(TARGET_DIR)/usr/lib/libretro/panda3ds_libretro.so
endef

$(eval $(cmake-package))
