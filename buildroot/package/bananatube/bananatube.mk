################################################################################
#
# bananatube -- a native dual-screen YouTube client for the RG DS and RG DS Plus.
#
# Nothing here spawns a Python interpreter on the search path: the search and the metadata are one libcurl
# request parsed with cJSON, and a stream URL is had from an Invidious instance or YouTube's own InnerTube
# API (see docs/ARCHITECTURE.md). On a 1 GB handheld that difference is the whole project: a yt-dlp per
# keystroke would cost ~70 MB and 3-5 seconds.
#
# The binary takes the DRM master from bananad exactly as BananaDS does. Its launcher is
# /usr/libexec/banana/run-bananatube, and it appears on the shelf through the app folder installed below --
# which needs one entry in /etc/banana/systems.conf: see the repository's system/systems.conf.snippet.
#
################################################################################

# A commit, not a branch: $(call github,...) builds an
# https://github.com/<user>/<repo>/archive/<version>/<repo>-<version>.tar.gz URL, which is the tarball
# download method. Pairing it with SITE_METHOD = git hands that whole URL to `git remote add origin` and the
# fetch fails ("repository .../archive/main/ not found") -- the wrong combination, and no upstream package
# makes it. Pinning a commit also makes the build reproducible and hashable (bananatube.hash).
#
# To build a newer one: git rev-parse HEAD in the bananatube checkout, put it here, and refresh the hash with
#   make bananatube-update-hash  (or download the tarball and sha256sum it).
BANANATUBE_VERSION = 954262132f333813afd3cbebc3db99db4e887f41
BANANATUBE_SITE = $(call github,jesussaves-ai,bananatube,$(BANANATUBE_VERSION))
BANANATUBE_LICENSE = GPL-3.0-or-later
BANANATUBE_LICENSE_FILES = LICENSE
# libdrm (the panels), Mesa/Panfrost (GBM + EGL on the top screen), libcurl + cJSON (the backends),
# libjpeg (thumbnails), FreeType (text), zlib
BANANATUBE_DEPENDENCIES = libdrm mesa3d-panfrost libcurl cjson jpeg freetype zlib mpv host-pkgconf

BANANATUBE_CONF_OPTS = \
	-DCMAKE_BUILD_TYPE=Release \
	-DBANANATUBE_KMS=ON \
	-DBANANATUBE_TESTS=OFF \
	-DBANANATUBE_MPV=ON

define BANANATUBE_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(BANANATUBE_BUILDDIR)/bananatube $(TARGET_DIR)/usr/bin/bananatube
	$(INSTALL) -D -m 0755 $(@D)/system/run-bananatube $(TARGET_DIR)/usr/libexec/banana/run-bananatube
	# the shelf app: a folder with a marker the menu lists, and its name (the tile's art would go in media/)
	$(INSTALL) -d $(TARGET_DIR)/usr/share/banana/apps/bananatube
	$(INSTALL) -m 0644 $(@D)/system/bananatube.gamelist.xml \
		$(TARGET_DIR)/usr/share/banana/apps/bananatube/gamelist.xml
	touch "$(TARGET_DIR)/usr/share/banana/apps/bananatube/BananaTube.app"
endef

$(eval $(cmake-package))
