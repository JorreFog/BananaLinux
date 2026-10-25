# Game companions

Settings > Other systems > **Game companion** (off by default; `game_companion` in settings.conf): while a Game Boy
Advance game plays on one screen through RetroArch, the game panel on the other shows seven pages about it, in the
Pokémon companion's look: **Status, Detail, Battle, Map, Bag, Collection, Progress**, with a **Game** tab for the
panel's own tiles and achievements. The Pokémon games (Ruby, Sapphire, Emerald, FireRed, LeafGreen and the hacks built
on them) keep the Pokémon companion (`pokemon_companion`); the game companion never takes them.

The companion idea is PokeDaisy's (https://github.com/lidor30/pokedaisy, by Lidor Itzhari).

## How it works (ui/src/gamecomp.{h,cpp}, the panel in ui/src/settings.cpp `gc*`)
- **Matching** (at the game's start, before RetroArch runs): the ROM header's code (0xAC) and title (0xA0), and its
  CRC-32 (free for a .zip: the zip's directory; for a .gba only worked out when a hack/translation definition could
  match). `/usr/share/banana/companions/index.tsv` lists every definition's keys, so this reads one small file.
- **Reading**: like the Pokémon companion, `READ_CORE_MEMORY` on RetroArch's stdin and the answers from its stdout
  (run-retroarch's `BANANA_RA_OUT` pipe), a set every 250 ms, the next only once the last was answered. Only the values
  the page on screen needs (and every page's `when`) are read, coalesced (addresses within 512 bytes share a read, up
  to 4 KB). A value behind a pointer is read where the pointer was in the last set; when it moves, the old value stays
  for one set and the set is asked again at once. ROM addresses (name tables) are read from the ROM file on a thread.
  gpSP (the default GBA core) maps EWRAM and IWRAM only.
- **Drawing**: only when a value changed (the engine compares each set with the last). Touch on the panel: tabs,
  the pager of a page longer than the panel, a list's row (its fields in full; a tap goes back). The game's buttons
  stay the game's.

## Adding a game
1. Write `<slug>.json` (format below); check it with `python3 tools/companions/validate.py <file>`.
2. `python3 tools/companions/install.py <folder with defs>` copies the valid ones into
   `system/rootfs/usr/share/banana/companions/` and rewrites `index.tsv`.
3. Host test: `make test` (ui/tests/gamecomp-test.cpp and the game panel run in tests/run.sh use tests/companions/).
4. On a handheld, without a new system: put the definition in `/data/banana/companions/` on the card (looked at
   before the system's folder; no index.tsv needed there, every file is read), turn Game companion on, start the game
   from the menu with Second screen on. banana-ui's log (`/data/banana/logs/banana-ui.log`) says which file matched.

## The definition format (schema 1)

Schema 1 (the first one the companion data was written in). Check every def with `python3 tools/companions/validate.py`
(no args: all of `defs/`; or give files). A def that fails validation is not installed.

The companion idea is PokeDaisy's (Lidor Itzhari). The Pokémon games (header codes AXVE AXPE BPEE BPRE BPGE and hacks
built on them) keep the Pokémon companion: the engine never uses a definition for them.

### File
One JSON file per game: `defs/<slug>.json`, slug = the zip name without `.zip`, lowercased, every run of non
`[a-z0-9]` characters -> `-`, no leading/trailing `-` (v1's one-dash-per-character slug, `gauntlet---dark-legacy`, is accepted too) (e.g. `Advance Wars 2 - Black Hole Rising.zip` ->
`advance-wars-2-black-hole-rising.json`). Translations/ zips: the manifest's slug; on a clash with a main zip
(only `metroid-fusion`) prefix `tr-` (`tr-metroid-fusion.json`, matched by `crc32` with `"exclusive": true`).
UTF-8, plain JSON (no comments).

### Top level
| key | type | required | meaning |
|---|---|---|---|
| `schema` | 1 | yes | |
| `title` | string | yes | shown in the panel's header |
| `match` | object | yes | see Matching |
| `accent` | `"#rrggbb"` | yes | the game's colour (tab underline, bars, headings) |
| `quality` | `live-verified` / `live-unverified` / `reference-only` | yes | honest: verified = seen changing in a running game |
| `sources` | [string] | yes (may be empty) | where addresses/facts come from |
| `vars` | object | no (`{}`) | name -> Var; names `[A-Za-z_][A-Za-z0-9_]*` |
| `enums` | object | no | name -> {key: label}; keys decimal (`"12"`) or hex (`"0x0c"`) |
| `pages` | [Page] | yes | exactly the 7 page ids, each once (any order; the panel orders them) |
| `notes` | string | no | free text for maintainers, not shown |
| `genre`, `credit` | string | no | documentation (not shown) |

### Matching (`match`)
The engine reads the running ROM's header: game code (4 chars at 0xAC) and title (12 chars at 0xA0, trailing NULs
and spaces removed). CRC32 is of the whole `.gba` (as in No-Intro dats; for a zip the member's CRC from the zip
directory, so it costs nothing).
- `code`: string or [string] (several regions/revisions), e.g. `"AWRE"`.
- `title12`: string, the header title (optional, tightens `code`).
- `crc32`: [8-hex-digit string], lowercase or uppercase.
- `zip`: the collection's zip name (documentation; also matched against the ROM's file name).
- `exclusive`: bool (default false). True = match only by `crc32` (use for hacks/translations whose header is the
  base game's, so the base game's def doesn't take them, and vice versa).

Order: a def whose `crc32` lists the ROM's CRC wins; else `code` (+ `title12` if given) among non-exclusive defs; else
the ROM file name equal to `zip` (minus extension). Header codes of unlicensed/homebrew carts can be junk ("0000",
"    "): use `crc32` for those.

### Vars
A var is read from the game's memory (`addr`), or computed (`expr`), or a constant (`value`).
| key | meaning |
|---|---|
| `addr` | GBA bus address, `"0x0300_1A20"` style string (underscores allowed) or integer |
| `type` | `u8 u16 u32 s8 s16 s32 bcd8 bcd16 bcd32` (little endian; bcdN = N bits of packed BCD, low byte = lowest digits), `bool` (u8 != 0), `text` |
| `len` | `text` only: bytes (1-64, default 16) |
| `enc` | `text` only: `ascii` (default), or `charmap:<enum>` (each byte looked up in that enum; missing bytes -> `?`) |
| `term` | `text` only: terminator byte (default 0; -1 none) |
| `ptr` | `[pointerAddr, offset]`: read the u32 at pointerAddr, add offset, read there (pointer 0 or outside the readable ranges -> the var is "not available"). Offset may be negative. |
| `mask` `shift` `add` `mul` `div` | post-processing in this order: `(raw & mask) >> shift`, then `* mul`, `/ div` (integer, toward zero), `+ add`. `mask` hex string or int. |
| `expr` | computed from other vars, see Expressions. A var with `expr` has no `addr`. |
| `value` | a constant (number or string) |

Readable address ranges (what RetroArch's gpSP and mGBA cores map):
- EWRAM `0x02000000-0x0203FFFF`, IWRAM `0x03000000-0x03007FFF`: live, read about 4 times a second.
- ROM `0x08000000-0x09FFFFFF`: read from the ROM file by banana-ui (constant: name tables, level names). A `ptr`
  whose pointer (in RAM) points into the ROM is followed too.
- Nothing else (no I/O, VRAM, OAM, palette, SRAM: gpSP doesn't map them).
A whole var (addr .. addr+size-1) must lie inside one range.

"Not available" (unreadable pointer, RetroArch error, not yet read): numbers draw as `-`, bars empty, `when` false.

### Expressions
`expr`, page `when`, widget `when`, list `rowWhen`, and the numeric fields `max`/`total` (when given as a string) are
expressions in banana-ui's expression language (the layout themes' one):
`+ - * / %`, `== != < <= > >=`, `&& || !`, `a ? b : c`, parentheses, numbers (decimal or `0x1F` hex), strings in `'single quotes'`, var names, and functions:
`min max clamp abs floor ceil round mod if` and, added for companions:
`band(a,b) bor(a,b) bxor(a,b) shl(a,n) shr(a,n) bit(v,n)` (0/1) `popcount(v)`.
`true`, `false`, `null` are reserved (not var names).
A string result is true when non-empty and not "0". Division is floating point (use `floor`).

### Enums
`"enums": {"areas": {"0": "Green Hill", "1": "Marble", "0x10": "Secret"}}` -> a `text` widget with `"enum": "areas"`
shows the label for the value (no label: the number). Also used by `charmap:` text encodings (key = byte).

### Pages
Exactly these 7 ids, each once:
| id | Pokémon analog | holds |
|---|---|---|
| `status` | party | the player/characters: HP, lives, level, stats, score, timer |
| `detail` | summary | one character/unit/vehicle in depth (a `list` the player taps a row of to open it) |
| `battle` | battle | the current enemy/boss/race: HP/state + a tip. Use `when` so it shows only in a fight |
| `map` | map | where: world/level/area, position, the game's structure |
| `bag` | bag | inventory, items, weapons, ammo, money, keys |
| `collection` | Pokédex | collectibles found/total |
| `progress` | badges | bosses beaten, stages cleared, completion, next goal |

Page keys: `id`, `title` (shown as the page heading), `tab` (optional short tab label, <= 9 chars; default = title),
`when` (expression; false -> the tab is greyed and the page says why; `battle` with a false `when` is hidden from the
tab bar like the Pokémon battle tab), `widgets` (non-empty list), `reference` (bool; default: true when no widget on
the page uses a memory var. A reference page carries a "Reference" badge).

Never an empty page: a page with nothing live holds static reference text (controls, item list, level list, boss
weaknesses) in your own words.

### Widgets
Every widget may have `when` (expression; false -> not drawn) and `label`.
| type | keys | draws |
|---|---|---|
| `number` | `value` (var name or expression), `format` | `label ......... 1234` |
| `text` | `value`, `enum` (optional) or a string var | `label ......... Green Hill` |
| `bar` | `value`, `max` (number, var or expression), `color` (`#rrggbb`, optional) | a labelled bar with `v/max` |
| `flags` | `value` (an int var), `items` [string] (bit 0 first; `""` skips a bit), `invert` (bool) | chips: lit = have |
| `count` | `value`, `total` (number/expression), `bits` (bool: popcount of value; default false) | `label  7 / 12` + a thin bar |
| `list` | `base`, `stride`, `n` (number or expression), `fields`, `rowWhen`, `title` | rows (tap one: its fields in full) |
| `static` | `text` (string; `\n` = new paragraph), `title` (optional) | wrapped paragraph |
| `grid` | as `flags`, drawn as a grid of small cells with the item names (for >12 bits) | |
| `kv` | `rows`: [[label, text], ...] static two-column table (controls, item effects) | |

`format` (number): a pattern with `{}` (`"${}"`, `"{}%"`, `"x{}"`), or a named one: `time60` (a frame count at
60/s -> `h:mm:ss`), `secs` (seconds -> `m:ss`), `hex`, `pad2`/`pad3`/`pad6` (zero padded), `thousands` (`12,345`).

`list`: `base` is an address (or `{"ptr": [pointerAddr, offset]}`), `stride` bytes per row, `n` rows (max 64).
`fields`: [{`name`, `label`, `off`, `type`, `len`/`enc` (text), `mask` `shift` `add` `mul` `div`, `enum`,
`format`}]; the first field is the row's name. `rowWhen` sees each field by its `name` (and every var): e.g.
`"rowWhen": "hp > 0"`. The whole table (`stride * n` bytes, <= 4096) must lie in one readable range.

### Size and cost
The engine coalesces vars into few `READ_CORE_MEMORY` reads (addresses within 512 bytes merge), about 4 sets a
second; a pointer var is read where the pointer pointed in the last set (one more set when it moved). Keep each def under ~64 distinct regions and a few KB read in total.

### Example
See `ui/tests/companions/` (real definitions: Castlevania: Aria of Sorrow and Golden Sun read live values, Tetris Worlds is reference pages).
