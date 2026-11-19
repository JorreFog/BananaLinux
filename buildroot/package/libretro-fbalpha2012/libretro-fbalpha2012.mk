################################################################################
#
# libretro-fbalpha2012
#
################################################################################

LIBRETRO_FBALPHA2012_VERSION = 0ce31536bef3162fe7e69ff5f555334ec4913cef
LIBRETRO_FBALPHA2012_SITE = $(call github,libretro,fbalpha2012,$(LIBRETRO_FBALPHA2012_VERSION))
LIBRETRO_FBALPHA2012_LICENSE = FB Alpha (non-commercial)
LIBRETRO_FBALPHA2012_LICENSE_FILES = svn-current/trunk/src/license.txt

define LIBRETRO_FBALPHA2012_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D)/svn-current/trunk platform=unix -f makefile.libretro CC="$(TARGET_CC)" CXX="$(TARGET_CXX)"
endef

# its license asks for its full text beside the program
define LIBRETRO_FBALPHA2012_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/svn-current/trunk/fbalpha2012_libretro.so $(TARGET_DIR)/usr/lib/libretro/fbalpha2012_libretro.so
	$(INSTALL) -D -m 0644 $(@D)/svn-current/trunk/src/license.txt $(TARGET_DIR)/usr/share/licenses/fbalpha2012/license.txt
endef

$(eval $(generic-package))
