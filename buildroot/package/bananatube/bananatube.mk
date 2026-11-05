################################################################################
#
# bananatube -- a native dual-screen YouTube client for the RG DS and RG DS Plus.
#
# Nothing here spawns a Python interpreter: the search and the stream URL are plain HTTP requests parsed with
# cJSON (an Invidious/Piped instance, then YouTube's own InnerTube API -- see docs/ARCHITECTURE.md). On a 1 GB
# handheld that is the whole point: a yt-dlp run would cost ~70 MB and several seconds.
#
# The binary takes DRM master from the menu exactly as BananaDS does. Its launcher is
# /usr/libexec/banana/run-bananatube, and it appears on the shelf through the app folder installed below --
# with the `[bananatube]` entry in the image's systems.conf and the `[app]` marker in the theme's systems.cfg.
#
################################################################################

# A commit, not a branch: $(call github,...) builds an
# https://github.com/<user>/<repo>/archive/<version>/<repo>-<version>.tar.gz URL, which is the tarball
# download method. Pairing it with SITE_METHOD = git hands that whole URL to `git remote add origin` and the
# fetch fails ("repository .../archive/main/ not found") -- the wrong combination, and no upstream package
# makes it. Pinning a commit also makes the build reproducible and hashable (bananatube.hash).
#
# To build a newer one: git rev-parse HEAD in the bananatube checkout, put it here, and refresh the hash with
#   sha256sum $(BR2_DL_DIR)/bananatube-<version>.tar.gz > package/bananatube/bananatube.hash
# (or download the tarball from the GitHub archive URL first).
BANANATUBE_VERSION = 141c9e3d15497311316026975be72ddcc0cb4dae
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
