################################################################################
#
# ldacbt
#
################################################################################

# Sony's LDAC encoder from AOSP (libldac, Apache-2.0) and its adaptive bit rate library, built standalone: libldacBT_enc
# and libldacBT_abr with their pkg-config files, which bluez-alsa's --enable-ldac looks for (external.mk). The release
# tarball carries the libldac submodule.
LDACBT_VERSION = 2.0.2.6
LDACBT_SOURCE = ldacBT-$(LDACBT_VERSION).tar.gz
LDACBT_SITE = https://github.com/EHfive/ldacBT/releases/download/v$(LDACBT_VERSION)
LDACBT_LICENSE = Apache-2.0
LDACBT_LICENSE_FILES = LICENSE
LDACBT_INSTALL_STAGING = YES
# the A55 has an FPU: the floating-point encoder, not the 32-bit fixed-point one
LDACBT_CONF_OPTS = -DLDAC_SOFT_FLOAT=OFF

$(eval $(cmake-package))
