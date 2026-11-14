################################################################################
#
# shaderc
#
################################################################################

# Google's shaderc (GLSL to SPIR-V), which ARMSX2's Vulkan renderer links (find_package(Shaderc)). shaderc builds
# glslang, SPIRV-Tools and SPIRV-Headers from its third_party folder; those are extra downloads here, at the commits
# shaderc v2026.4's DEPS file names, so it doesn't depend on Buildroot's own SPIRV-Tools version.
SHADERC_VERSION = 2026.4
SHADERC_SITE = $(call github,google,shaderc,v$(SHADERC_VERSION))
SHADERC_LICENSE = Apache-2.0, BSD-3-Clause, MIT
SHADERC_LICENSE_FILES = LICENSE
SHADERC_INSTALL_STAGING = YES
SHADERC_DEPENDENCIES = host-python3
SHADERC_GLSLANG_VERSION = e1b562a8bed273a02f30b59b66a5d499793cede5
SHADERC_SPIRV_TOOLS_VERSION = ef96ed763b43b59b33b31b362f09a02b729fa1c9
SHADERC_SPIRV_HEADERS_VERSION = 04fd3caa1e8267e4d95c806cad901181728e1006
SHADERC_EXTRA_DOWNLOADS = \
	https://github.com/KhronosGroup/glslang/archive/$(SHADERC_GLSLANG_VERSION).tar.gz \
	https://github.com/KhronosGroup/SPIRV-Tools/archive/$(SHADERC_SPIRV_TOOLS_VERSION).tar.gz \
	https://github.com/KhronosGroup/SPIRV-Headers/archive/$(SHADERC_SPIRV_HEADERS_VERSION).tar.gz

# the three sources into third_party/, where shaderc's CMake looks for them
define SHADERC_THIRD_PARTY_EXTRACT
	$(foreach d,glslang:$(SHADERC_GLSLANG_VERSION) spirv-tools:$(SHADERC_SPIRV_TOOLS_VERSION) \
		spirv-headers:$(SHADERC_SPIRV_HEADERS_VERSION),\
		mkdir -p $(@D)/third_party/$(firstword $(subst :, ,$(d)))$(sep)\
		$(call suitable-extractor,$(lastword $(subst :, ,$(d))).tar.gz) \
			$(SHADERC_DL_DIR)/$(lastword $(subst :, ,$(d))).tar.gz | \
			$(TAR) --strip-components=1 -C $(@D)/third_party/$(firstword $(subst :, ,$(d))) $(TAR_OPTIONS) -$(sep))
endef
SHADERC_POST_EXTRACT_HOOKS += SHADERC_THIRD_PARTY_EXTRACT

SHADERC_CONF_OPTS = -DSHADERC_SKIP_TESTS=ON -DSHADERC_SKIP_EXAMPLES=ON -DSHADERC_SKIP_COPYRIGHT_CHECK=ON \
	-DSHADERC_ENABLE_WERROR_COMPILE=OFF -DSPIRV_SKIP_EXECUTABLES=ON -DSPIRV_SKIP_TESTS=ON \
	-DENABLE_GLSLANG_BINARIES=OFF -DGLSLANG_TESTS=OFF -DSPIRV_WERROR=OFF \
	-DPython3_EXECUTABLE=$(HOST_DIR)/bin/python3

$(eval $(cmake-package))
