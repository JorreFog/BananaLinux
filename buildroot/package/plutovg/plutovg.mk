################################################################################
#
# plutovg
#
################################################################################

# PlutoVG, the 2D vector library plutosvg draws with (ARMSX2 asks for plutovg 1.1.0 or newer)
PLUTOVG_VERSION = 1.3.3
PLUTOVG_SITE = $(call github,sammycage,plutovg,v$(PLUTOVG_VERSION))
PLUTOVG_LICENSE = MIT
PLUTOVG_LICENSE_FILES = LICENSE
PLUTOVG_INSTALL_STAGING = YES
PLUTOVG_CONF_OPTS = -DPLUTOVG_BUILD_EXAMPLES=OFF -DBUILD_SHARED_LIBS=ON

$(eval $(cmake-package))
