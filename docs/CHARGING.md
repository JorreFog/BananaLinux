# RG DS Plus: charging input limit

The normal USB input current limit remains 1.5 A. On the Plus, **Settings >
Sleep & power > Higher charging limit** requests 2 A for the connected charger.
Press A twice to confirm that both the supply and cable are rated for 5 V / 3 A.
**Normal charging limit** restores 1.5 A.

This is a request to the charger's input-current limiter, not a promised battery
current or charging speed. The 4.5 V input-voltage floor is unchanged, so a weak
supply or resistive cable can still limit charging. It does not request a higher
USB voltage or add a USB PD or Quick Charge protocol.

The choice is deliberately not saved. The driver restores normal charging on
plug events, suspend and orderly shutdown, and again when it starts. A bus
failure can prevent a reset; a forced power-off or panic does not run the
shutdown hook. Changing the supply needs a new explicit
selection. No startup service re-applies it.

The kernel enables the RK817's hardware charge thermal foldback, independently
of the menu. This senses PMIC die temperature; it does not measure battery
temperature. The Plus battery ceiling is 2 A and 4.35 V. The menu helper refuses
the higher input profile if it cannot verify those ceilings or the required
writable kernel property. It changes neither battery limit.

For SSH, the same helper can report or select the limit:

```sh
/usr/libexec/banana/charging status
/usr/libexec/banana/charging higher --confirm-5v-3a
/usr/libexec/banana/charging normal
```

## Updates and rollback

The driver patch is applied by the existing Buildroot kernel patch series. The
menu and helper are in the ordinary read-only root filesystem. The normal
BananaOS image/update package therefore carries them together, without a card
boot hook, custom kernel-copy script, or updater change.

An older kernel without the writable property rejects the helper operation. A
replacement device tree with the old 3.5 A cell ceiling also rejects the higher
profile. An older BananaOS slot has no saved charging preference to replay.

## Validation

`sh system/tests/charging-test.sh` exercises the real helper against private
power-supply files: explicit confirmation, current limits, battery ceilings,
missing properties, malformed values, unsupported boards, repeated requests and
normal restoration. It writes no actual power-supply files. Kernel validation
must additionally cover probe failures, register-write/readback failures,
concurrent disconnects, suspend/resume, shutdown and removal.

Universal images identify the handheld through U-Boot's `banana.board=` argument.

Before treating the higher limit as a charging improvement, measure input power,
battery current and temperatures on an RG DS Plus with a rated supply and cable,
through a complete charge and under load. Confirm normal limits after changing
supplies, sleeping and restarting. The current limits alone establish no speed
or thermal result.

References: [RK817 charger driver](https://github.com/gregkh/linux/blob/v7.2.7/drivers/power/supply/rk817_charger.c),
[GammaOS thermal foldback implementation](https://github.com/TheGammaSqueeze/rg_ds_kernel/blob/21bd5bac4314011713b16d1970bcbaad3ad1c4b0/drivers/power/supply/rk817_charger.c),
and the [vendor-derived Plus charger configuration](https://github.com/TheGammaSqueeze/rg_ds_kernel/blob/21bd5bac4314011713b16d1970bcbaad3ad1c4b0/arch/arm64/boot/dts/rockchip/rk3568-deep-lp3-rk817.dtsi).
