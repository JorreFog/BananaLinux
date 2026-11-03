# GBA Pokémon companion = the DS companion - design

Status: owner said "sätt igång" (2026-11-03) after the request below; the choices marked *(call)* are the
coordinator's, made without asking (the owner opted out of questions).

## Intent

Owner, 2026-11-03: "återskapa samma detaljerade companion som används i ds som pokemon companion för gba". The GBA
Pokémon companion (ui/src/pokemon.*, the game panel's Party/Battle/Map/Bag/Dex/Guide tabs) gets the DS companion's
depth and look: the DS pages (Battle, Party, Team, Here, Guide, Dex, Log), its logic (damage %, catch helper, wild
IVs/nature/shiny, team score, catch now, TM planner, EV tracker, evolution watch, daycare, roamers, encounter odds,
journey log, challenge mode, notices) and BananaOS's own look - for the GBA games, on the GBA's second panel.

## Goals

- Games: Ruby, Sapphire, Emerald, FireRed, LeafGreen, English (AXVE AXPE BPEE BPRE BPGE, every revision on the
  server library / the owner's handhelds: FireRed/LeafGreen Rev 1, Ruby/Sapphire Rev 2, Emerald).
- The DS pages and logic, reused (ui/src/nds/*), fed by a **Gen III data source**: the ROM's data as an nds::RomData
  (names, types, base stats, moves, items, learnsets, TMs, evolutions, Pokédex, encounters by map, trainers, the
  game's own front sprites normal + shiny) and the running game as an nds::Live (party, battle, bag, money, badges,
  Pokédex, place, trainer id, play time, daycare, roamers where the game has them).
- Shown on the game panel (the panel the GBA game is not on, touch), where today's GBA pages are: the DS page set,
  plus today's **Map** (the game's own region map) and the panel's **Game** tiles (save/load/menu/fast-forward/quit)
  kept as tabs *(call: nothing the GBA companion has today is lost)*.
- Guide data for the five games (gyms, E4, rivals, steps, secrets, version exclusives, evolution notes) as
  /usr/share/banana/companions/gba-pokemon/<game>.json in the DS guide schema, facts in our own words.

## Non-goals (for now)

- ROM hacks (CFRU: Pokémon Unbound...) *(call)*: they keep today's GBA pages unchanged until the Gen III source
  handles their tables (Fairy, 900+ species, expanded bag) - a later step, not a loss.
- Other languages; RetroArch-only paths beyond what today's companion already does (BananaFront's shm and
  RetroArch's pipes both stay).
- Writing game memory.

## Anti-goals

- Costing the game frame rate or input latency (the companion reads ~4 sets a second, as today).
- Shipping anything of the games: all names, sprites and text read from the player's ROM at run time.
- Wrong advice shown as fact (Gen III rules: physical/special by type, its own catch formula and items).

## Approach *(call)*

1. **Reuse the DS model/logic/pages with a Gen III source** (chosen): nds::Gen gets III; the Gen IV/V branches in
   logic.cpp treat III as IV where the rules match, with Gen III's own where they differ (category by type, item
   ids, catch formula). A gen3 adapter turns pkmn::RomTables (+ new ROM reads) into nds::RomData and pkmn::Snapshot
   (+ new RAM reads) into nds::Live. The game panel hosts nds::Pages on its screen.
2. Port the DS features into pokemon.cpp's pages one by one: two code bases to keep, the look not shared. Rejected.
3. A new Gen III RAM reader written from scratch for nds::Live: duplicates today's tested reads. Rejected.

## Workstreams

- **rom** (agent): `ui/src/gba/gen3rom.{h,cpp}`: `std::shared_ptr<nds::RomData> gen3::readRomData(...)` from the
  ROM bytes + pkmn::RomTables; species by national number; sprites.
- **guide** (agent): the five JSON files + their validation with tools/companions/nds-validate.py.
- **live + host** (coordinator): model/logic Gen III, `ui/src/gba/gen3live.*` (Snapshot -> Live, extra RAM reads),
  a Session path fed with Live, the game panel hosting the pages, Settings, tests, device check.

## Verification

Host tests per part (make test). On the RG DS Plus: each of the five games through the real launcher, the pages
checked against the game's own screens (party, a wild battle, bag, Pokédex), the companion's cost against the GBA
stress ROM with the companion on/off. Games only as the last validation (house rule).
