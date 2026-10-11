################################################################################
#
# mesa3d-panfrost
#
################################################################################

MESA3D_PANFROST_VERSION = 25.0.7
MESA3D_PANFROST_SOURCE = mesa-$(MESA3D_PANFROST_VERSION).tar.xz
MESA3D_PANFROST_SITE = https://archive.mesa3d.org
MESA3D_PANFROST_LICENSE = MIT, SGI, Khronos
MESA3D_PANFROST_LICENSE_FILES = docs/license.rst licenses/MIT licenses/SGI-B-2.0
MESA3D_PANFROST_INSTALL_STAGING = YES
MESA3D_PANFROST_PROVIDES = libegl libgles libgbm
MESA3D_PANFROST_DEPENDENCIES = host-bison host-flex host-python-mako host-python-packaging host-python-pyyaml libdrm zlib

# Panfrost alone, on DRM with no window system; no LLVM (Bifrost's compiler is Mesa's own), no OpenCL, no video
MESA3D_PANFROST_CONF_OPTS = \
	-Dgallium-drivers=panfrost \
	-Dvulkan-drivers= \
	-Dplatforms= \
	-Dllvm=disabled \
	-Dglx=disabled \
	-Dglvnd=disabled \
	-Degl=enabled \
	-Dgbm=enabled \
	-Dopengl=true \
	-Dgles1=disabled \
	-Dgles2=enabled \
	-Dshared-glapi=enabled \
	-Dgallium-va=disabled \
	-Dgallium-vdpau=disabled \
	-Dgallium-xa=disabled \
	-Dgallium-nine=false \
	-Dgallium-opencl=disabled \
	-Dgallium-rusticl=false \
	-Dmicrosoft-clc=disabled \
	-Dvideo-codecs= \
	-Dxmlconfig=disabled \
	-Dexpat=disabled \
	-Dzstd=disabled \
	-Dvalgrind=disabled \
	-Dlibunwind=disabled \
	-Dlmsensors=disabled \
	-Dbuild-tests=false \
	-Dtools=

$(eval $(meson-package))
