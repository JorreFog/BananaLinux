# BananaOS's menu for ROCKNIX

This is a ROCKNIX package, `banana-frontend`. It makes BananaOS's dual-screen menu (banana-ui) the frontend of a ROCKNIX
image on a two-screen handheld, starting with the AYN Thor (SM8550). banana-ui runs under sway as two fullscreen
windows, one per screen, and starts games through ROCKNIX's `runemu.sh`. Its systems are read from ROCKNIX's
`es_systems.cfg`.

```sh
git clone https://github.com/ROCKNIX/distribution rocknix
git -C rocknix checkout "$(sed -n 's/^commit=//p' rocknix/ROCKNIX)"   # the pin this is checked against
tools/rocknix-banana.sh rocknix SM8550     # the package, banana-ui's source, ADDITIONAL_PACKAGES
cd rocknix && make docker-SM8550          # ROCKNIX's own build (see its README)
```

Run `tools/rocknix-banana.sh` again after changing `ui/`.

| File | What |
|---|---|
| `packages/ui/banana-frontend/package.mk` | builds banana-ui with `SDL=1`, installs it and its assets |
| `packages/ui/banana-frontend/system.d/bananasway.service` | the frontend under sway, in place of `essway.service` |
| `packages/ui/banana-frontend/sources/start_banana.sh` | the systems from `es_systems.cfg`, then banana-ui on both outputs |
| `packages/ui/banana-frontend/sources/es-systems-to-banana.py` | `es_systems.cfg` to banana-ui's `systems.conf` |
| `packages/ui/banana-frontend/system.d/essway.service.d/banana.conf` | EmulationStation's unit runs the menu too (ROCKNIX's scripts start it by name, after Steam) |
| `packages/ui/banana-frontend/sources/rocknix-device` | the handheld's board and hardware profile into `/run/banana` (`thor` on the AYN Thor) |
| `packages/ui/banana-frontend/sources/hw.conf`, `hw-thor.conf` | the shared profile (services mode) and the AYN Thor's lines |
| `ROCKNIX` | the ROCKNIX commit this is made and checked against |
| `tests/run.sh` | the converter on a sample `es_systems.cfg`, then every other check here (run by `make test`) |
| `tests/thor.sh` | the Thor's pieces in services mode, and the installer on a made-up checkout |

The plan and its status are in `docs/THOR-ROCKNIX.md`.
