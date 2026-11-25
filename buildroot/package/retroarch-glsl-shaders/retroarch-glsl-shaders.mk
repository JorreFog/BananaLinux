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
	$(RETROARCH_GLSL_SHADERS_FIX_PRECISION)
endef

# The vertex stage's uniforms at highp. Most of these shaders declare their shared uniforms (OutputSize, TextureSize...)
# mediump in the vertex stage but highp in the fragment stage where the GPU has it; Panfrost lowers the vertex ones to
# 16 bits, the two stages' declarations no longer match, and the link fails ("uniform `OutputSize' declared as type
# `vec2' and type `f16vec2'"): RetroArch falls back to its stock shader (zfast_crt_nogeo_svideo, #71). A vertex
# shader always has highp in GLES, and costs nothing at a few vertices a frame; most fragment shaders are left as they are.
# Fast LCD also needs highp in the fragment stage: mediump rounds the pixel-grid coordinates enough to change the
# border brightness across an integer-scaled viewport. Use the same precision in both stages of that shader.
define RETROARCH_GLSL_SHADERS_FIX_PRECISION
	find $(TARGET_DIR)/usr/share/libretro/shaders/glsl -name '*.glsl' -exec sh -c 'awk '\''BEGIN { v = 0 } \
		FNR == 1 { lcd = FILENAME ~ /\/handheld\/shaders\/zfast_lcd\.glsl$$/ } \
		/defined\(VERTEX\)/ { v = 1 } /defined\(FRAGMENT\)/ { v = 0 } \
		{ if ((v || lcd) && $$0 ~ /#define COMPAT_PRECISION mediump/) sub(/mediump/, "highp"); print }'\'' "$$1" > "$$1.new" && \
		mv "$$1.new" "$$1"' _ {} \;
endef

$(eval $(generic-package))
