# Achievements list - design (2026-11-12)

## Intent
The owner asked for "a page that displays all achievements for a game and info about the achievement, and shows
which are unlocked and which aren't", reachable **both** from the library (without starting the game) and while
playing. Today the menu only keeps "12 / 40 achievements" per game, and the RetroArch/BananaFront game panel shows
progress, the latest unlocks and the ones closest to done - never the whole list.

## Goals
- One list page in banana-ui: every core achievement of the game (server display order), each with its badge, title,
  description and points; unlocked ones marked as such with the date (softcore or hardcore), locked ones visibly
  locked (greyed badge - the server's `<badge>_lock.png` - and a "Locked" mark). A header with
  "unlocked / total" and "points earned / total points".
- Opened from the game's page in the library (X on a game, `Settings::openGame`): a new "Achievements" row.
- Opened while a RetroArch/BananaFront game runs, from the bottom game panel (a visible control; a tap on the
  achievements box already opens the States view, so it can't be that).
- Works for a game never played: banana-ui identifies the ROM with rcheevos' hashing (rhash, MIT, vendored the way
  BananaFront/SuperDrastic vendor it) and asks the server for the game id (`r=gameid&m=<hash>`).
- Offline: the last list and unlocks are cached on the card and shown with "as of <date>".

## Non-goals / anti-goals
- No in-game list inside BananaDS / SuperDrastic menus yet (second step, separate work in ~/superdrastic).
- Never start a RetroAchievements session or post anything from the library page (read-only: `r=patch`,
  `r=unlocks` for h=0 and h=1, `r=gameid`). Starting a session from the menu would show the player as playing.
- No hardcore/softcore toggles or achievement-hunting features beyond showing the list.
- Must not slow the library or the game panel: all network and hashing on threads, never in the paint loop.

## Constraints
- RetroAchievements account from banana/accounts.conf (same user/token the game panel uses). No account -> the page
  says how to add one (Settings > accounts). No network and no cache -> says so.
- Every new string through `lang::tr` (translations fall back to English; translators are credited elsewhere).
- Hashing a large ROM (DS up to 512 MB, PS1 discs) is slow: hash once, keep `<rom key> -> hash, game id` in a
  cache file; the in-game panel's identified id (RaWatch) is written to the same cache.

## Approaches considered
- **A (picked by the owner):** one list page in the menu, opened from the game page and from the game panel; menu
  identifies games itself with rcheevos; cached for offline.
- B: same but only remember ids from past plays - smaller, but never-played games show nothing.
- C: A plus lists inside the DS emulators' in-game menus - deferred to a second step.

## Left to the builder
Exact layout (follow the existing settings list look and the in-game panel look), how the in-game control looks,
cache file format, sort/filter (server order by default; a filter is optional).
