################################################################################
#
# plutosvg
#
################################################################################

# PlutoSVG, ARMSX2's SVG and colour-emoji renderer (0.0.7, the version it asks for), with FreeType, on package/plutovg
# (found by find_package, so its own copy of plutovg isn't built)
PLUTOSVG_VERSION = 0.0.7
PLUTOSVG_SITE = $(call github,sammycage,plutosvg,v$(PLUTOSVG_VERSION))
PLUTOSVG_LICENSE = MIT
PLUTOSVG_LICENSE_FILES = LICENSE
PLUTOSVG_INSTALL_STAGING = YES
PLUTOSVG_DEPENDENCIES = plutovg freetype
PLUTOSVG_CONF_OPTS = -DPLUTOSVG_ENABLE_FREETYPE=ON -DPLUTOSVG_BUILD_EXAMPLES=OFF -DBUILD_SHARED_LIBS=ON

$(eval $(cmake-package))
