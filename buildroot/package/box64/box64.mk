################################################################################
#
# box64: x86_64 programs on the AYN Thor (Wine, for PC games)
#
################################################################################

BOX64_VERSION = v0.4.4
BOX64_SITE = $(call github,ptitSeb,box64,$(BOX64_VERSION))
BOX64_LICENSE = MIT
BOX64_LICENSE_FILES = LICENSE
# its build makes its wrapper tables with the host's Python
BOX64_DEPENDENCIES = host-python3

# the ARM64 recompiler tuned for the Snapdragon 8 Gen 2 (SD8G2: its own flags for the Cortex-X3/A715/A510 set); Box32
# for 32-bit x86 code, which the Thor's cores can't run as 32-bit ARM (the X3 and A715 have no AArch32): Wine's
# WoW64 runs a 32-bit Windows game inside the 64-bit Wine, on Box32. No binfmt registration here: pc-games starts
# box64 by name. NOGIT: built from a release archive.
BOX64_CONF_OPTS = \
	-DARM_DYNAREC=ON \
	-DSD8G2=ON \
	-DBOX32=ON \
	-DBOX32_BINFMT=OFF \
	-DNOGIT=ON \
	-DNO_CONF_INSTALL=ON \
	-DCMAKE_BUILD_TYPE=RelWithDebInfo

# Box64 installs the x86 libraries it runs as x86 code instead of handing over to the system's (x64lib and x86lib in
# its sources: libunwind, which Wine's ntdll needs, libgcc_s, libstdc++...) where it looks for them by itself. They
# aren't the target's architecture, which Buildroot's check of every installed program would refuse
# (and with them its own x86_64 bash, /usr/bin/box64-bash, which it runs a game's shell scripts with)
BOX64_BIN_ARCH_EXCLUDE = /usr/lib/box64-x86_64-linux-gnu /usr/lib/box64-i386-linux-gnu /usr/bin/box64-bash

# its settings per program (box64.box64rc: the known-good switches for Wine, Steam and many games) where it looks
define BOX64_INSTALL_RC
	$(INSTALL) -D -m 0644 $(@D)/system/box64.box64rc $(TARGET_DIR)/etc/box64.box64rc
endef
BOX64_POST_INSTALL_TARGET_HOOKS += BOX64_INSTALL_RC

$(eval $(cmake-package))
