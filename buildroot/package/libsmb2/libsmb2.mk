################################################################################
#
# libsmb2
#
################################################################################

LIBSMB2_VERSION = 6.2
LIBSMB2_SITE = $(call github,sahlberg,libsmb2,libsmb2-$(LIBSMB2_VERSION))
# the library (lib, include) is LGPL; the examples, not built, BSD
LIBSMB2_LICENSE = LGPL-2.1+
LIBSMB2_LICENSE_FILES = COPYING LICENCE-LGPL-2.1.txt
LIBSMB2_INSTALL_STAGING = YES
# NTLM (libsmb2's own) logs in to Windows, macOS and NAS shares; no Kerberos, no examples
LIBSMB2_CONF_OPTS = -DENABLE_LIBKRB5=OFF -DENABLE_GSSAPI=OFF -DENABLE_EXAMPLES=OFF

$(eval $(cmake-package))
