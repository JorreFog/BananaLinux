# Live previews in the browser (WebAssembly) — design

The editor previews a theme today by asking `serve.py` to run the real engine (`banana-ui --headless`) and
handing back PNGs (`render.js`). That is exact and needs a host. The alternative, so the page previews on its
own with no server, is to compile the same engine to WebAssembly and draw into a `<canvas>` every frame.

**Status: designed, not built.** There is no Emscripten toolchain in the environment this was developed in, and
the binding below has to be run to be trusted, so it is not committed half-done. `serve.py` remains the working
preview. What follows is the exact shape to build it, so it is a contained job for whoever has `emcc`.

## What to compile

The layout engine is small and self-contained — the same files `ui/Makefile` builds for the frontend, minus the
device parts:

| File | Why |
| --- | --- |
| `ui/src/layout.cpp` | the engine: elements, lists, hooks, `LayoutMenu` |
| `ui/src/expr.cpp` | the expression language |
| `ui/src/json.cpp` | the JSON reader (comments, trailing commas, key order) |
| `ui/rnds/RndsRaster.cpp` | text and picture rasterization |
| `ui/rnds/RndsUI.cpp` | the palette, `Backend`/`Source` interfaces, `UI` |
| a new `theme-wasm.cpp` | the binding: a `Backend` and a `Source`, plus the JS entry points |

FreeType comes from Emscripten's port: `-sUSE_FREETYPE=1`.

## What the binding has to provide

1. **A `Backend`** (`ui/rnds/RndsUI.h`, seven methods) that draws into the canvas:
   `upload`, `draw`, `fill`, `pushClip`, `popClip`, `beginScreen`, `endScreen`. `beginScreen(0|1)` selects the
   top or bottom panel; each is 640×480 (or 1024×768). The simplest faithful implementation keeps two
   `ImageData` buffers and a 2-D context, and `upload` returns an offscreen canvas per texture. `soft.cpp` is
   the reference for how the methods are meant to behave, but it writes device pixels, so the canvas version can
   be much smaller (rounded rects and image blits).
2. **A `Source`** (`ui/rnds/RndsUI.h`) returning made-up systems and games — the same data the editor already
   shows in its sample scope (`sampleSys`/`sampleGame` in `app.js`). A few systems, a few games each, one
   favourite, one with box art, is enough to exercise every hook.
3. **Entry points** exported to JS:
   - `themeLoad(themeJson)` → parse with `json.cpp`, build a `LayoutMenu`, return errors (the `theme:` lines).
   - `themeRender(view, systemIndex, gameIndex, timeMs)` → advance the menu and draw both screens.
   - `themePress(button)` / `themeTouch(screen, x, y)` → feed the engine's input, so the editor can drive it.
   - `themeAssets(bytesByPath)` → write fonts/pictures into `MEMFS` at the theme's relative paths.

## Build (with emscripten installed)

```sh
em++ -O2 -std=c++17 -sUSE_FREETYPE=1 -sALLOW_MEMORY_GROWTH=1 \
    -I ui/src -I ui/rnds \
    ui/src/layout.cpp ui/src/expr.cpp ui/src/json.cpp \
    ui/rnds/RndsRaster.cpp ui/rnds/RndsUI.cpp \
    tools/theme-studio/wasm/theme-wasm.cpp \
    --preload-file ui/assets/banana/fonts@/fonts \
    -sEXPORTED_FUNCTIONS='["_themeLoad","_themeRender","_themePress","_themeTouch","_themeAssets"]' \
    -sEXPORTED_RUNTIME_METHODS='["ccall","cwrap","FS"]' \
    -o tools/theme-studio/wasm/banana-theme.js
```

The editor then loads `banana-theme.js` in place of (or beside) `render.js`: when the module is present it
draws in real time, and `serve.py` stays as the fallback for exact stills. Keeping both is deliberate — the
server path proves the WASM path, since both run the same `layout.cpp`.

## Why not re-implement the elements in JavaScript

Text layout, picture fitting and the easing functions would all have to match the engine pixel for pixel; the
port would drift with every engine change. Compiling the engine itself cannot drift. (The expression language is
the one part already ported — `expr.js` — and it is kept honest by `tests/parity.cpp`, so the editor can evaluate
values live without WASM.)
