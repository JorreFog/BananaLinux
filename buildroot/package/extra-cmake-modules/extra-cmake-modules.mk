################################################################################
#
# extra-cmake-modules
#
################################################################################

# KDE's extra CMake modules, which ARMSX2 and melonDS use to find Wayland (FindWayland). Buildroot's copy
# (kf5-extra-cmake-modules) can only be had with Qt 5, which can't be built beside Qt 6; this one stands alone.
# CMake files only: into the staging sysroot, nothing on the target.
EXTRA_CMAKE_MODULES_VERSION_MAJOR = 6.18
EXTRA_CMAKE_MODULES_VERSION = $(EXTRA_CMAKE_MODULES_VERSION_MAJOR).0
EXTRA_CMAKE_MODULES_SITE = https://download.kde.org/stable/frameworks/$(EXTRA_CMAKE_MODULES_VERSION_MAJOR)
EXTRA_CMAKE_MODULES_SOURCE = extra-cmake-modules-$(EXTRA_CMAKE_MODULES_VERSION).tar.xz
EXTRA_CMAKE_MODULES_LICENSE = BSD-3-Clause
EXTRA_CMAKE_MODULES_LICENSE_FILES = LICENSES/BSD-3-Clause.txt
EXTRA_CMAKE_MODULES_INSTALL_STAGING = YES
EXTRA_CMAKE_MODULES_INSTALL_TARGET = NO
EXTRA_CMAKE_MODULES_CONF_OPTS = -DBUILD_HTML_DOCS=OFF -DBUILD_MAN_DOCS=OFF -DBUILD_QTHELP_DOCS=OFF -DBUILD_TESTING=OFF

$(eval $(cmake-package))
