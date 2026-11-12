BananaOS on the AYN Thor: OpenABL
=================================

The Thor's own bootloader (ABL) only starts Android. BananaOS boots through OpenABL, an open replacement for it
(https://github.com/OpenHandheldCollective/OpenABL, the release ROCKNIX uses). It is flashed ONCE; Android keeps
working (OpenABL starts it when no card is in, or from its menu).

THIS WRITES THE THOR'S BOOTLOADER PARTITIONS. Back up the old ABL first and keep the backup off the handheld.

From fastboot (a computer with Android's platform tools; hold Volume - while powering on for the fastboot menu):

    fastboot flash abl_a abl_signed-SM8550.elf
    fastboot flash abl_b abl_signed-SM8550.elf
    fastboot reboot

Or, rooted, from Android itself: ROCKNIX's backup_abl.sh and flash_abl.sh do the same with dd to
/dev/block/by-name/abl_a and abl_b (https://github.com/ROCKNIX/distribution, projects/ROCKNIX/packages/tools/openabl).

Then: power off, put the BananaOS card in, power on. If OpenABL asks for the device model, pick the AYN Thor
(qcs8550-ayn-thor).

STATUS: the Thor port is in bring-up (docs/THOR-PORT.md in the BananaOS repository). Expect missing pieces.
