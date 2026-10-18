################################################################################
#
# libretro-cheats-nds
#
################################################################################

# libretro-database's Action Replay cheats for the Nintendo DS (cht/Nintendo - Nintendo DS at 12c21cf79f1e, 4204
# files), in the image for BananaDS's Cheats (read-only, nothing on the card). The whole database is ~800 MB, so this
# folder alone is kept here as a tarball (2.2 MB): tar -cf - "cht/Nintendo - Nintendo DS" | xz -9
LIBRETRO_CHEATS_NDS_VERSION = 12c21cf
LIBRETRO_CHEATS_NDS_SOURCE = libretro-cheats-nds-$(LIBRETRO_CHEATS_NDS_VERSION).tar.xz
LIBRETRO_CHEATS_NDS_SITE = $(BR2_EXTERNAL_BANANA_PATH)/package/libretro-cheats-nds
LIBRETRO_CHEATS_NDS_SITE_METHOD = file
LIBRETRO_CHEATS_NDS_LICENSE = CC-BY-SA-4.0
LIBRETRO_CHEATS_NDS_STRIP_COMPONENTS = 0

define LIBRETRO_CHEATS_NDS_INSTALL_TARGET_CMDS
	mkdir -p $(TARGET_DIR)/usr/share/libretro/cheats
	cp -a "$(@D)/cht/Nintendo - Nintendo DS" $(TARGET_DIR)/usr/share/libretro/cheats/
endef

$(eval $(generic-package))
