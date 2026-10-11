################################################################################
#
# superdrastic
#
################################################################################

# Built from source (its build.sh, with Buildroot's GCC instead of clang). It links libdrm only; EGL/GLES (shaders),
# ALSA, libcurl and zlib are loaded at run time when present.
SUPERDRASTIC_VERSION = 930c4c5bb3e904557cd8cc42a01a4e27834f5ba1
SUPERDRASTIC_SITE = https://github.com/JorreFog/SuperDrastic.git
SUPERDRASTIC_SITE_METHOD = git
SUPERDRASTIC_LICENSE = MIT
SUPERDRASTIC_LICENSE_FILES = LICENSE THIRD_PARTY.md
SUPERDRASTIC_DEPENDENCIES = libdrm host-python3

SUPERDRASTIC_RC = src/third_party/rcheevos
# Without Buildroot's large-file defines: resume.c interposes both open and open64 (fopen and fopen64), and with
# _FILE_OFFSET_BITS=64 glibc renames open to open64, so the file would define the symbol twice. off_t is 64 bits
# on AArch64 whatever the define.
# And NDEBUG: audio.c includes stb_vorbis under "#pragma GCC visibility push(hidden)", which re-includes assert.h,
# and the toolchain's GCC 14 makes that redeclaration of __assert_fail hidden, so the library can't link it from
# libc. A release build has no use for the asserts anyway (upstream builds with clang, which doesn't do this).
SUPERDRASTIC_CFLAGS = $(filter-out -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64,$(TARGET_CFLAGS)) -DNDEBUG
define SUPERDRASTIC_BUILD_CMDS
	cd $(@D) && $(HOST_DIR)/bin/python3 src/rast/kerngen.py src/rast/rast_kern.S
	cd $(@D) && $(TARGET_CC) $(SUPERDRASTIC_CFLAGS) -O2 -shared -fPIC -Wall -Wno-unused-function \
		-DDSFLIP_VERSION="\"$$(cat VERSION)-banana\"" -DRC_DISABLE_LUA -DRC_CLIENT_SUPPORTS_HASH \
		-I$(STAGING_DIR)/usr/include/libdrm -I$(SUPERDRASTIC_RC)/include -I$(SUPERDRASTIC_RC)/src -Isrc/third_party/stb \
		-o libsuperdrastic.so \
		src/dsflip.c src/shader.c src/audio.c src/ra.c src/ui.c src/volume.c src/cpugov.c src/resume.c src/menu.c \
		$(SUPERDRASTIC_RC)/src/rc_client.c $(SUPERDRASTIC_RC)/src/rc_compat.c $(SUPERDRASTIC_RC)/src/rc_util.c \
		$(SUPERDRASTIC_RC)/src/rc_version.c $(SUPERDRASTIC_RC)/src/rcheevos/*.c $(SUPERDRASTIC_RC)/src/rapi/*.c \
		$(SUPERDRASTIC_RC)/src/rhash/*.c \
		src/rast/rast.c src/rast/b0.c src/rast/fused.c src/rast/fused_neon.c src/rast/fused_asm.c src/rast/defer.c \
		src/rast/hr.c src/rast/comp.c src/rast/rast_kern.S src/rast/spec/*.c \
		$(TARGET_LDFLAGS) -ldrm -lpthread -lm
endef

define SUPERDRASTIC_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/libsuperdrastic.so $(TARGET_DIR)/usr/lib/superdrastic/libsuperdrastic.so
	$(INSTALL) -D -m 0755 $(@D)/runner/superdrastic-run $(TARGET_DIR)/usr/lib/superdrastic/superdrastic-run
	mkdir -p $(TARGET_DIR)/usr/lib/superdrastic/shaders
	cp $(@D)/shaders/*.frag $(TARGET_DIR)/usr/lib/superdrastic/shaders/
	cp $(@D)/VERSION $(TARGET_DIR)/usr/lib/superdrastic/VERSION
endef

$(eval $(generic-package))
