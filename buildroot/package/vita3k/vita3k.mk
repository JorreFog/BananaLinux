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
# libraries (SDL, dynarmic, glslang, ffmpeg-core, boost...) are submodules and come with the git download. ROCKNIX lists
# Qt among its dependencies but Vita3K doesn't use it; it isn't one here.
VITA3K_VERSION = ba5c2029c96a08db63cfe04736156af481d9c137
VITA3K_SITE = https://github.com/Vita3K/Vita3K.git
VITA3K_SITE_METHOD = git
VITA3K_GIT_SUBMODULES = YES
VITA3K_LICENSE = GPL-2.0+
VITA3K_LICENSE_FILES = COPYING.txt
VITA3K_SUPPORTS_IN_SOURCE_BUILD = NO
VITA3K_DEPENDENCIES = host-pkgconf libevdev openssl zlib dbus alsa-lib libegl libgles wayland libxkbcommon \
	vulkan-headers
VITA3K_CONF_OPTS = -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DUSE_DISCORD_RICH_PRESENCE=OFF \
	-DUSE_VITA3K_UPDATE=OFF -DXXHASH_BUILD_XXHSUM=ON -DXXH_X86DISPATCH_ALLOW_AVX=OFF

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
