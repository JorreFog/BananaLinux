# The Thor's kernel patches beyond ROCKNIX's

Applied after ROCKNIX's (`../linux-patches`, which `tools/sync-rocknix-sm8550.sh` replaces wholesale), by
`buildroot/external.mk`. From [Banana Pocket](https://github.com/jesussaves-dev/Banana-Pocket) (BananaOS for the
Retroid Pocket Classic, SM4450; MIT, the Banana contributors), `buildroot/board/retroid-pocket-classic/linux-patches`
at `2f4da8bf`, kept under their names there. They're for parts the Thor shares with the Pocket Classic: the
RSInput pad on a GENI UART, the AW88166 amplifiers on the ADSP, the battery manager on the ADSP.

Checked here: in this order they apply on top of ROCKNIX's 71 patches on Linux 7.2, and the drivers they touch
build for arm64 with the Thor's config. Not yet run on a Thor.

| Patch | Why the Thor wants it |
|---|---|
| 0024 serial: qcom-geni: restart RX after the read command terminates itself | the RSInput pad talks over a GENI UART (uart15); the Pocket saw RX stop |
| 0031 aw88166: support changing sample rate and bit width | the amplifiers follow the stream instead of only the ACF's baked-in format |
| 0032 aw88166: reduce log spam | |
| 0033 aw88166: remove fade in/out on start/stop | |
| 0062 aw88166: lock on the I2S clocks, not the ACF re-upload | the Pocket's ~10-20 s silent boot (its docs/BOOT-SOUND.md): the amplifier start retried before the I2S clocks ran, re-uploading the ACF each time |
| 0051 qcom_battmgr: allow setting the USB input current limit | the Thor's charger is the ADSP's battery manager too |
| 0052 qcom_battmgr: report the USB adapter type | |
| 0063 q6apm: report the graph close and how long it took | the DSP graph's timing, for the sound bring-up |

Left out on purpose:

- **0034**, which makes the aw88166 volume usable. It inverts the volume control. ROCKNIX's Thor UCM sets
  `SPK_L/R PCM Playback Volume` to 0, meaning no attenuation, so with 0034 the speakers would go silent. Take it
  together with a UCM change, as the Pocket did with its own profile.
- **0039**, soundwire EE0. It changes the link manager's execution environment for every SoC, and that is
  SM4450-firmware specific. Look at it again for the Thor's sleep, M3.
- **0061**, edt-ft5x06 following the panel's power. It does nothing until the Thor's device tree links each
  touchscreen to its panel. Worth it if the touchscreens fail after the panels power down.
- **0059**, rsinput digital triggers. It's opt-in, and the Thor's triggers are analog.
- The Pocket's SM4450 enablement: clocks, interconnect, display, GPU, remoteproc.
