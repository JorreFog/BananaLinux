################################################################################
#
# thor-emulators
#
################################################################################

# Nothing to build: the AYN Thor's emulators are packages of their own (Config.in beside this file lists them). This
# file only holds what their .mk files share.

# The folders ROCKNIX's emulator settings and patches name (/storage/...) as BananaOS has them on the card (/data/...):
# BIOS files, states and screenshots first, then the games, then each emulator's own folder. The templates in
# package/<emulator>/config are already written this way; this is for files that come from an emulator's own tree.
THOR_EMULATORS_DATA_PATHS = $(SED) 's|/storage/roms/bios|/data/bios|g; s|/storage/roms/savestates|/data/states|g; \
	s|/storage/roms/screenshots|/data/screenshots|g; s|/storage/roms|/data/roms|g; \
	s|/storage/\.config/|/data/|g; s|/storage/\.local/share/|/data/|g'

# install_templates <package>: package/<package>/config (ROCKNIX's SM8550 settings, written for BananaOS) to
# /usr/share/<package>, which the emulator's launcher copies to /data/<package> on the first run
define THOR_EMULATORS_INSTALL_TEMPLATES
	mkdir -p $(TARGET_DIR)/usr/share/$(1)
	cp -a $(BR2_EXTERNAL_BANANA_PATH)/package/$(1)/config/. $(TARGET_DIR)/usr/share/$(1)/
endef
