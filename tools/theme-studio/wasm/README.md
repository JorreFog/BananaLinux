# The theme engine in the browser (WebAssembly)

`banana-engine.js` is banana-ui's theme engine compiled to WebAssembly: what the web studio's live preview draws
with, so the page works on its own (from file:// or any static host) and shows exactly what the handheld shows.

| File | What it is |
| --- | --- |
| `theme-wasm.cpp` | The binding. It drives `ui/src/layout.cpp` (and `expr.cpp`, `json.cpp`, `library.cpp`, `theme.cpp`, `soft.cpp`, `zip.cpp`, the rnds rasterizer) as `banana-ui --headless` does: a `SoftBackend` draws both screens in memory, a `Library` reads the made-up test library. C calls for JS (`bt_init`, `bt_load`, `bt_frame`, `bt_pixels`, `bt_press`, `bt_touch`, `bt_script`, `bt_eval`, ...). |
| `build.sh` | Builds `banana-engine.js` with Emscripten: one file with the wasm, the built-in fonts and icons, `systems.conf` and two libraries inlined (`ui/tests/fixture.sh`'s, at `/fx`, and the same with a game for every system, at `/fxall`). |
| `sources.sh` | The hash of everything it is built from; the build writes it into the output and `tests/engine.test.js` says when they differ. |
| `banana-engine.js` | The build (generated: don't edit). |

```sh
git clone https://github.com/emscripten-core/emsdk ~/emsdk && ~/emsdk/emsdk install latest && ~/emsdk/emsdk activate latest
source ~/emsdk/emsdk_env.sh
sh tools/theme-studio/wasm/build.sh          # ~15 s; needs ImageMagick for the fixture's box art
node tools/theme-studio/tests/engine.test.js  # the same pixels as ui/banana-ui, on every example theme, at both sizes
```

The theme being edited is written by `engine.js` to `/theme/theme.json` in the engine's memory file system, with its
attached files beside it at their relative paths, and loaded as `banana-ui --theme` would load it (`applyTheme`, then
a `LayoutMenu`). Each animation frame `bt_frame` updates the menu, finishes its picture jobs and draws; the page
copies the two screens onto canvases when they changed.

Not in it: the Settings screen and the keyboard (START, SELECT), the sounds, and the Carts and Pixel themes; the
splash. The clock is the browser's.
