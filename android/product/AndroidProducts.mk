#
# BananaOS on LineageOS: the products this directory defines.
#
# The build finds this file anywhere under vendor/ (it is checked out at vendor/banana/android/product by
# android/manifest/banana.xml). The product's name has to start with "lineage_": LineageOS's envsetup only sets
# LINEAGE_BUILD (and with it BoardConfigLineage.mk, LineageOS's sepolicy and its packaging) for such names.
#
#   source build/envsetup.sh
#   breakfast banana_thor          # = lunch lineage_banana_thor-<LineageOS's release>-userdebug
#   m bacon                        # or: mka bacon
#

PRODUCT_MAKEFILES := \
    $(LOCAL_DIR)/lineage_banana_thor.mk
