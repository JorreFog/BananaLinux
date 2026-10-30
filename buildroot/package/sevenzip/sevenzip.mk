################################################################################
#
# sevenzip
#
################################################################################

SEVENZIP_VERSION = 26.03
SEVENZIP_SOURCE = 7z$(subst .,,$(SEVENZIP_VERSION))-src.tar.xz
SEVENZIP_SITE = https://www.7-zip.org/a
SEVENZIP_LICENSE = LGPL-2.1+ with the unRAR restriction (RAR codecs), BSD-3-Clause (LZFSE, Zstandard decoders), BSD-2-Clause (xxHash)
SEVENZIP_LICENSE_FILES = DOC/License.txt

# (the archive has no top folder)
define SEVENZIP_EXTRACT_CMDS
	$(call suitable-extractor,$(SEVENZIP_SOURCE)) $(SEVENZIP_DL_DIR)/$(SEVENZIP_SOURCE) | $(TAR) -C $(@D) $(TAR_OPTIONS) -
endef

# 7-Zip's makefiles for gcc on arm64 (its assembler code with them), with Buildroot's compilers and flags (*_BASE2:
# before 7-Zip's own, its -O2 after them; its LDFLAGS only add -DNDEBUG); warnings not errors, for a gcc newer than
# 7-Zip's
define SEVENZIP_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D)/CPP/7zip/Bundles/Alone2 -f ../../cmpl_gcc_arm64.mak \
		CC="$(TARGET_CC)" CXX="$(TARGET_CXX)" CFLAGS_BASE2="$(TARGET_CFLAGS)" CXXFLAGS_BASE2="$(TARGET_CXXFLAGS)" \
		LDFLAGS="$(TARGET_LDFLAGS)" CFLAGS_WARN_WALL=-Wall
endef

define SEVENZIP_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/CPP/7zip/Bundles/Alone2/b/g_arm64/7zz $(TARGET_DIR)/usr/bin/7zz
endef

$(eval $(generic-package))
