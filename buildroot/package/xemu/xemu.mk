################################################################################
#
# xemu
#
################################################################################

# xemu, the original Xbox emulator (QEMU with the Xbox's GPU and audio), SDL 3 and Vulkan, on Wayland. Ported from
# ROCKNIX's xemu-sa (packages/emulators/standalone/xemu-sa at 6cf94dba, GPL-2.0): the same commit, its configure options
# (an i386 system emulator only, everything QEMU has beyond what xemu needs off) and its patches (0001 xemu's glslang
# subproject, 0002 the CPU speed in the on-screen display, 0003 integer scaling). Differences: SDL 3 is package/sdl3
# (found by pkg-config, so xemu doesn't build its own), no GTK (xemu needs none on Linux), no X11.
# Known gap, as in ROCKNIX's build: xemu's Meson subprojects (genconfig, imgui, implot, glslang, SPIRV-Reflect, volk,
# VulkanMemoryAllocator, tomlplusplus, xxhash, berkeley-softfloat, keycodemapdb...) are wrap files that Meson downloads
# when xemu configures (--enable-download); they aren't Buildroot downloads, so this build needs the network. QEMU's
# ROM submodules aren't needed and aren't fetched (the tarball, not git).
XEMU_VERSION = 6095054b7d02dfac25fdc4a8ba9300358046e860
XEMU_SITE = $(call github,xemu-project,xemu,$(XEMU_VERSION))
XEMU_LICENSE = GPL-2.0+
XEMU_LICENSE_FILES = LICENSE COPYING
XEMU_DEPENDENCIES = host-pkgconf host-python3 host-meson host-ninja libglib2 pixman zlib sdl3 libepoxy \
	libsamplerate libpcap slirp openssl alsa-lib libegl libgles vulkan-headers vulkan-loader

# xemu doesn't build with NDEBUG (ROCKNIX's note)
XEMU_CFLAGS = $(filter-out -DNDEBUG,$(TARGET_CFLAGS))

define XEMU_CONFIGURE_CMDS
	cd $(@D) && $(TARGET_CONFIGURE_OPTS) CFLAGS="$(XEMU_CFLAGS)" CXXFLAGS="$(filter-out -DNDEBUG,$(TARGET_CXXFLAGS))" \
		PKG_CONFIG="$(PKG_CONFIG_HOST_BINARY)" PATH=$(BR_PATH) \
		./configure --target-list=i386-softmmu --cross-prefix=$(TARGET_CROSS) \
		--python=$(HOST_DIR)/bin/python3 --meson=$(HOST_DIR)/bin/meson --ninja=$(HOST_DIR)/bin/ninja \
		--enable-download --enable-sdl --enable-opengl --disable-gtk --enable-alsa --disable-pa \
		--enable-trace-backends=nop --disable-kvm --disable-xen --disable-werror --disable-curl \
		--disable-vnc --disable-docs --disable-tools --disable-guest-agent --disable-tpm --disable-replication \
		--disable-capstone --disable-libiscsi --disable-spice --disable-user --disable-stack-protector \
		--disable-glusterfs --disable-curses --disable-gnutls --disable-nettle --disable-gcrypt \
		--disable-crypto-afalg --disable-virglrenderer --disable-vhost-net --disable-vhost-crypto \
		--disable-vhost-user --disable-virtfs --disable-snappy --disable-bzip2 --disable-vde --disable-seccomp \
		--disable-numa --disable-lzo --disable-smartcard --disable-usb-redir --disable-bochs --disable-cloop \
		--disable-dmg --disable-vdi --disable-vvfat --disable-qcow1 --disable-qed --disable-parallels \
		--disable-hvf --disable-whpx --disable-pie --with-default-devices
endef

define XEMU_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D)/build
endef

# the emulator and ROCKNIX's SM8550 settings (xemu.toml: Vulkan; the BIOS, EEPROM and hard disk image paths in
# /data/bios/xemu). The hard disk image (xqemu's free, formatted one, which ROCKNIX downloads at install) isn't in the
# image: run-xemu says where it goes.
define XEMU_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/build/qemu-system-i386 $(TARGET_DIR)/usr/bin/xemu
	$(call THOR_EMULATORS_INSTALL_TEMPLATES,xemu)
endef

$(eval $(generic-package))
