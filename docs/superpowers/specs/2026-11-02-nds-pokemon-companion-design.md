# DS Pokémon companion - design

Status: draft for the owner's approval (2026-11-02). Mockups: `~/nds-companion/mockups/` (battle-plus.png,
team-plus.png, dex-plus.png, icon-rgds-badge-zoom.png; scripts beside them). Memory/ROM research:
`~/nds-companion/RESEARCH.md` (in progress when this was written; its findings decide the per-game tables).

## Intent

The GBA Pokémon companion (ui/src/pokemon.*) shows the party and battle help on the second screen while a GBA Pokémon
game runs. The owner wants the same for the DS Pokémon games, as a **large upgrade**: "feature packed with all the
info you can gather from the memory and anything you can find on the internet like guides, tactics, secrets", in a
look that is BananaOS's own rather than a copy of the GBA companion. A DS game uses both screens, so the companion
cannot have a panel of its own: it lives behind a small icon on the touch screen.

## Goals

- Games: **all nine main-series DS Pokémon games** (owner): Diamond, Pearl, Platinum, HeartGold, SoulSilver, Black,
  White, Black 2, White 2, in the releases on the server PC's library (Romhost, ~/romhost-data/library/nds on
  192.168.50.96, synced to the handhelds): Diamond (Europe, English, Rev 5), Pearl (USA, Rev 5), Black 2 (USA/Europe,
  DSi Enhanced), the others as dumped there; each release's layout verified on its own. The owner's handhelds
  have HeartGold, Platinum and Black 2 (Plus) and Platinum (RG DS).
- **Icon:** a small, half-see-through icon tucked into the bottom-right corner of the DS bottom screen while a
  supported game runs in BananaDS. A tap on it opens the companion and is never passed to the game. Only shown when the
  companion recognised the game.
- **Badge:** a small red badge with a count on the icon when the companion has something for the player: a guide tip
  (a goal or a thing to think about before what's coming), a Pokémon the Team check recommends is catchable in this
  area, hidden items/secrets nearby, a roamer on this route. Opening the companion lands on the page that raised it;
  a seen item clears and does not return. No sound, no blinking.
- **Companion screen:** replaces the game's bottom screen while open; the top screen keeps the game. The **game keeps
  running** (owner). A "Back to game" button sits where the icon is (a tap there opens and closes). **The game keeps its buttons**
  (owner): every button press still goes to the game while the companion is open, so the companion is driven by touch
  only (tabs, rows, scrolling by drag, the Normal/Shiny switch, Back to game); no button closes it.
- **Pages** (order: Battle, Party, Team, Here, Guide, Dex, Log):
  - **Battle** (mockup battle-plus.png): your active Pokémon vs the foe (types, HP, ability, who moves first, the
    trainer's remaining Pokémon), the four moves against this foe with effectiveness and an expected-damage bar against
    the foe's HP ("88-104%, likely KO", the best one outlined), what the foe's Pokémon knows (trainer data from the
    ROM). In wild battles also: the **catch helper** (catch chance per ball in the bag, from catch rate, HP, status,
    ball, time/place; what would raise it) and the wild Pokémon's **nature, IVs, Hidden Power, shiny**. Double battles:
    both foes.
  - **Party:** all six: stats, IVs/EVs, nature, ability, item, moves with PP, EXP to next level, friendship,
    **evolution watch** (levels to go, friendship meter, item/place/time conditions, in-game routes to trade
    evolutions where the game has one), the next moves learnt, the **EV tracker** (effort points per stat, gained from
    each defeated foe).
  - **Team** (mockup team-plus.png): a **team score 0-100** (type coverage, defence, levels vs the next big fight,
    moves; each a bar), the next gym/E4/rival with its ace's level and the concrete problems ("Normal and Fighting
    moves can't touch her team"), **Catch now**: Pokémon reachable at this point in the story, ranked by the gaps they
    fill, with where/when; the **TM planner** (TMs in the bag your team can learn that fill a gap).
  - **Here:** this area's wild Pokémon with **encounter odds** by time of day / swarm / Poké Radar / season, caught
    marks and "3 of 7 caught here", hidden items close by, trainers, the **roamer tracker** (each roamer's current
    route, live), Black 2's **Hidden Grottos** (what can appear in the nearest), the **daycare** (who, compatibility,
    steps to the egg).
  - **Guide:** the next goal and what to prepare, gym/E4 teams with counters, version exclusives, evolution methods
    by item/place/time, legendaries and events, secrets near the player. Written in our own words from facts; no
    guide text copied.
  - **Dex** (mockup dex-plus.png): every Pokémon of the game being played (493 in Platinum/HGSS, 649 in Black 2):
    caught/seen/unseen list; per entry the sprite **animated with the game's own frames** (Black 2: its animated
    sprites; Platinum/HGSS: their two-frame sprites), Normal/Shiny switch, number, category, Pokédex text, types,
    abilities, base stats, evolution chain with methods, learnset, where found in this game, weaknesses. Sprites,
    shiny palettes and text **from the ROM of the game being played** (owner: "borrow the sprites from the game that
    you are playing").
  - **Log:** the **journey log**, an automatic timeline of catches, evolutions, badges, first-time events, with play
    time, kept per game on the card.
  - **Challenge mode** (opt-in setting): Nuzlocke-style tracking of the first encounter per route and the Pokémon lost.
- **Look:** BananaOS's own (Pixelify Sans, the dark dotted background, cream-outlined panels with the lip, the
  theme's accent; type colours for chips), designed for the panel sizes (RG DS 640x480, Plus 1024x768) and following
  the active theme's palette and accent.

## Non-goals

- SuperDrastic (DraStic): it has no memory export; the icon simply doesn't appear there. BananaDS only.
- The spin-offs (Mystery Dungeon, Ranger, Conquest, Trozei!): owner chose main series only; each would be its own
  project later. Other DS games: the universal game companion's job.
- Releases not in the server library, and non-English ones (their RAM layouts and text tables differ; later).
- Changing the game (no cheats, no writes to game memory).

## Anti-goals (a failure even if it works)

- The icon, badge or companion costing the game frame rate, sound or input latency (measured on the DS stress ROM
  through the real launcher on both panels, companion closed and open, against a baseline).
- The icon blocking something the player needs (it covers only the corner of the battle POKéMON button; if testing
  on the three games finds a worse conflict, the placement moves - owner: "if the placement is bad then pick a
  different one, test it on different games").
- Shipping anything from the games (sprites, text, data tables, guide prose): everything is read from the player's
  own ROM at run time or written by us as facts.
- Wrong advice presented as fact: a value that couldn't be verified on a running game is not shown, or shown marked.

## Constraints

- House rules (bananalinux skill): no kernel/DTS/U-Boot changes; per-package builds; test on stress ROMs, games last;
  device safety; JorreFog identity; credit PokeDaisy for the idea (as the GBA companion does).
- Performance: the RK3566/RK3568's four A55 cores and 512 KiB shared cache; BananaDS's own threads are the priority.
- Text: the companion's own strings through lang::tr (English first; zh-cn/ru by the credited translators, never
  machine-translated as theirs); names and Pokédex text come from the ROM in the game's language.

## Approaches (owner picked 1)

1. **BananaDS shows the companion (chosen).** banana-ui reads the game (memory export already in BananaDS:
   `/run/banana/companion.shm`, bcomp, READ_CORE_MEMORY lines) and the ROM, and renders the companion pages
   off-screen into a shared image; BananaDS draws the icon + badge on its bottom-screen output and, while open, shows
   the shared image in place of the game's bottom screen and forwards touches to banana-ui. One mechanism, shared
   with BananaFront's planned bottom-screen menu (KAN-148). Cost: the icon blend every frame (tiny), the image copy
   only while open.
2. banana-ui draws on a display layer of its own over BananaDS's bottom screen. No copies, but BananaDS owns that
   screen and the VOP2 has few spare planes (BananaDS's own modes use them); a missing plane on one handheld would
   leave no fallback. Rejected as riskier.
3. BananaDS draws the whole companion itself: duplicates banana-ui's companion code and look. Rejected.

## How (decided so far)

- **banana-ui** (C++): a new `pokemon_nds` module beside pokemon.cpp: game detection by the ROM header's game code;
  the RAM reader on CompLink (pointer chains per game from RESEARCH.md, validated by checksums); the Gen IV/V
  decryption; a ROM reader (NitroFS FNT/FAT, NARC, BLZ for arm9/overlays where needed, Gen IV/V text decoding,
  NCGR/NCLR/NCER/NANR sprite + animation decoding, shiny palettes) caching what it decoded on the card; the pages; the
  team score and the catch-now ranking; the badge state; renders into the shared image at the bottom panel's size
  (only when something changed, at most a few times a second).
- **Story progress tables** (which areas are reachable at which point, gym/E4 teams' context, hidden items and
  secrets by map, evolution methods, grotto contents): per-game JSON data files written by us from facts (sources
  listed per file), installed in /usr/share/banana/companions/nds-pokemon/; anything the ROM itself has (trainer
  teams, encounter tables, learnsets, items) is read from the ROM instead.
- **BananaDS** (SuperDrastic repo, BananaDS pin): the bottom-screen overlay: icon + badge (state from the shm), the
  shared image while open, touch routing (icon rect -> toggle; open -> touches to banana-ui); buttons always go to the game.
- **Settings:** Settings > Nintendo DS > BananaDS > **NDS Pokémon companion (alpha)** (on/off; owner's name for it,
  the "(alpha)" stays until the owner drops it), Challenge mode (off/on); the icon's corner if testing shows the
  owner a reason to choose. The row's help says it is an early version and works with BananaDS only.

## Settled after the builder check (coordinator's calls, marked as such)

- **Research gates the RAM pages** (call): ~/nds-companion/RESEARCH.md (being written) is phase 0; the Dex, which
  needs only the ROM, can start before it. Host-side work uses copies of the owner's own ROMs on the PC (never
  committed; HeartGold is in ~/Filed/Games, Platinum and Black 2 come from the Plus); every RAM value is checked on a
  running game on the handhelds.
- **Story data** (call): written by the builders from web research (decompilations, Bulbapedia/Serebii facts, in our
  own words), one JSON file per game with `sources` and a `quality` per entry like the game companions'
  (`live-verified` / `reference-only`); the UI marks reference-only advice, and anything contradicted by the ROM or a
  running game is dropped.
- **Per-game storage key** (call): game code + trainer ID + secret ID (read from RAM at the start), so two playthroughs
  of one game keep separate logs, challenge records and badge state; the decode cache is keyed by the ROM's hash.
- **Shared overlay mechanism** (call): general, a versioned extension of the bcomp spec
  (~/banana-emu/common/COMPANION-SPEC.md) with a producer-side API BananaFront can use for KAN-148 later. BananaDS's
  source is in the SuperDrastic repo (~/superdrastic, `bananads/`; the shipped pin 664483a is on origin
  release/0.5.1-bd and ovn-ds/* branches carry companion.cpp); the BananaLinux package pins a SuperDrastic commit.
- **Badge triggers** (call): only triggers verified on a running game ship; the others stay off until verified.

## Left to the builder

- The shared-image format (RGB565 at panel size, double-buffered, a frame counter) and the touch/command channel in
  the shm (extend bcomp, versioned).
- The team-score weights (documented, tuned on real teams from the three games).
- Caching layout on the card; decode on first use, in the background, with a "reading your game" state.

## Verification

- Host: unit tests for decryption (known PKM samples from PKHeX's test data or made by our encoder), NARC/text/sprite
  decoders (against our own synthetic files), the score, the badge logic; headless renders of every page at 640x480
  and 1024x768 with fixture data.
- Device: DS stress ROM through run-bananads on both handhelds, companion off / closed / open: fps, drops, audio
  underruns, CPU per thread vs baseline. Then the final validation on all nine games (from the server library, copied to the
  handhelds for the test and removed after; the owner's own saves backed up first where a game is theirs) with their saves backed up first: every page against the game's own screens,
  the icon placement checked on the overworld, battle, menus, Pokétch/Pokégear/C-Gear, bag and PC screens.
- Release: **only when everything above is done and tested on all nine games** (owner: wait for everything), together
  with the 0.5.1.1 fixes, on the closed dev OTA with the beta password.
