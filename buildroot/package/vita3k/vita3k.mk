################################################################################
#
# vita3k
#
################################################################################

# Vita3K, the PlayStation Vita emulator (SDL with ImGui menus, Vulkan through Turnip), on Wayland. Ported from ROCKNIX's
# vita3k-sa (packages/emulators/standalone/vita3k-sa at 6cf94dba, GPL-2.0): the same commit, its build options and its
# patches (0001 a missing <cstdint>, from Vita3K upstream; 0002 fixed folders, written as BananaOS's: settings in
# /data/Vita3K, the Vita's storage (firmware, installed games, saves) in /data/roms/psvita/vita3k; 0003 a link fix;
# 0004 --exit-on-close, so the emulator ends with the game instead of going back to its game list). Its external
# libraries (SDL, dynarmic, glslang...) are submodules and come with the git download. ROCKNIX lists Qt among its
# dependencies but Vita3K doesn't use it; it isn't one here.
VITA3K_VERSION = ba5c2029c96a08db63cfe04736156af481d9c137
VITA3K_SITE = https://github.com/Vita3K/Vita3K.git
VITA3K_SITE_METHOD = git
VITA3K_GIT_SUBMODULES = YES
VITA3K_LICENSE = GPL-2.0+
VITA3K_LICENSE_FILES = COPYING.txt
VITA3K_SUPPORTS_IN_SOURCE_BUILD = NO
VITA3K_DEPENDENCIES = host-pkgconf libevdev openssl zlib dbus alsa-lib libegl libgles wayland libxkbcommon \
	vulkan-headers boost libcurl
# Boost (its filesystem library) is Buildroot's: without one in the system Vita3K builds its bundled Boost at configure
# time with b2 and the build machine's own g++, for the wrong machine. And the system's curl, or Vita3K's CMake clones
# curl's sources while it configures
VITA3K_CONF_OPTS = -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DUSE_DISCORD_RICH_PRESENCE=OFF \
	-DUSE_VITA3K_UPDATE=OFF -DXXHASH_BUILD_XXHSUM=ON -DXXH_X86DISPATCH_ALLOW_AVX=OFF -DVITA3K_FORCE_SYSTEM_BOOST=ON

# Vita3K's ffmpeg (external/ffmpeg, Vita3K's ffmpeg-core) is no source build: its CMake downloads the libraries its
# project built for that commit, which it asks git for. Buildroot's download has no .git folder, so the archive for the
# commit Vita3K pins (ccce45ff) is a download of this package (its hash in vita3k.hash; the git download itself has
# none) and goes where that CMake looks before it downloads
VITA3K_FFMPEG_CORE_VERSION = ccce45f
VITA3K_FFMPEG_CORE_ZIP = ffmpeg-linux-$(if $(BR2_aarch64),arm64,x64).zip
VITA3K_EXTRA_DOWNLOADS = \
	https://github.com/Vita3K/ffmpeg-core/releases/download/$(VITA3K_FFMPEG_CORE_VERSION)/$(VITA3K_FFMPEG_CORE_ZIP)
BR_NO_CHECK_HASH_FOR += $(VITA3K_SOURCE)
define VITA3K_FFMPEG_CORE_PREBUILT
	$(INSTALL) -D -m 0644 $(VITA3K_DL_DIR)/$(VITA3K_FFMPEG_CORE_ZIP) $(@D)/buildroot-build/external/ffmpeg.zip
endef
VITA3K_PRE_CONFIGURE_HOOKS += VITA3K_FFMPEG_CORE_PREBUILT

# Buildroot's Boost is shared libraries, where Vita3K's CMake asks for static ones only
define VITA3K_SHARED_BOOST
	$(SED) 's/set(Boost_USE_STATIC_LIBS ON)/set(Boost_USE_STATIC_LIBS OFF)/' $(@D)/CMakeLists.txt
endef
VITA3K_POST_PATCH_HOOKS += VITA3K_SHARED_BOOST

# what ROCKNIX's post_unpack does for cross-compiling: -isystem written as -I in the generated build files
define VITA3K_ROCKNIX_FIXUPS
	find $(@D) -name flags.make -exec $(SED) 's:isystem :I:g' {} \;
	find $(@D) -name build.ninja -exec $(SED) 's:isystem :I:g' {} \;
endef
VITA3K_POST_PATCH_HOOKS += VITA3K_ROCKNIX_FIXUPS

# the emulator as vita3k-sa (ROCKNIX's name), its shaders, data and translations beside the settings template in
# /usr/share/vita3k (Vita3K reads them from its settings folder, /data/Vita3K: run-vita3k links them there)
define VITA3K_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/buildroot-build/bin/Vita3K $(TARGET_DIR)/usr/bin/vita3k-sa
	$(call THOR_EMULATORS_INSTALL_TEMPLATES,vita3k)
	cp -a $(@D)/buildroot-build/bin/shaders-builtin $(@D)/buildroot-build/bin/data $(@D)/buildroot-build/bin/lang \
		$(TARGET_DIR)/usr/share/vita3k/
endef

$(eval $(cmake-package))
