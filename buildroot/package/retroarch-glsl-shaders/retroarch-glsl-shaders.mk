################################################################################
#
# retroarch-glsl-shaders
#
################################################################################

# libretro's GLSL shaders and presets (the kind RetroArch's "gl" driver runs), in the image so Shaders > Load Preset
# isn't empty (RetroArch's video_shader_dir points here)
RETROARCH_GLSL_SHADERS_VERSION = ce9a41242e27f41e16bc35c28943466cee3e8ff8
RETROARCH_GLSL_SHADERS_SITE = $(call github,libretro,glsl-shaders,$(RETROARCH_GLSL_SHADERS_VERSION))
RETROARCH_GLSL_SHADERS_LICENSE = various (per shader, mostly GPL-2.0+/public domain)

define RETROARCH_GLSL_SHADERS_INSTALL_TARGET_CMDS
	mkdir -p $(TARGET_DIR)/usr/share/libretro/shaders/glsl
	cd $(@D) && cp -a $$(ls -A | grep -vxE 'Makefile|\.git.*|configure|README.*') $(TARGET_DIR)/usr/share/libretro/shaders/glsl/
endef

$(eval $(generic-package))
