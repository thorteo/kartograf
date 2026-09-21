# Roadmap

The stated goal is to be better than TarkovQuestie, not merely equivalent. This file
records what that means concretely, in the order we intend to earn it.

## M0 — Foundations

- [ ] Toolchain established and a trivial window builds and runs (`docs/build.md`)
- [ ] `core/` builds and tests on Linux with no Windows headers
- [ ] **Projection validated against all 741 real screenshot positions** — the test
      described in `research/map-projection.md`. Nothing else is trustworthy until this
      passes.
- [ ] Log parser recognises all five lifecycle events in the fixture session
- [ ] Screenshot filename parser handles the full fixture set, including the `(N)`
      collision suffix

## M1 — The GPS core

- [ ] Transparent click-through overlay over the game in borderless mode
- [ ] **Rebindable toggle hotkey**, persisted, with conflict detection
- [ ] Automatic map detection from logs — the overlay opens the right map by itself
- [ ] Live position marker with heading, and a visible **age indicator** on the fix
- [ ] Automatic floor switching from elevation plus bounds
- [ ] Raid timer and in-game clock
- [ ] Excluded from screen capture by default

## M2 — Quests

- [ ] tarkov.dev task data cached locally, bundled snapshot for offline first run
- [ ] Objective markers on the map, filtered to the current map
- [ ] Active-quest list with distance and bearing to each objective
- [ ] **Required keys surfaced per objective** — which key to bring, before the raid
- [ ] Quest progress tracking, source to be decided (TarkovTracker sync, log-derived,
      or manual — this is still an open question)

## M3 — Beyond TarkovQuestie

Ideas, not commitments. Each needs to earn its place against the cost of screen clutter
and complexity.

- **Breadcrumb trail** — where you have already walked this raid, from successive fixes
- **Dead reckoning** — project forward from the last fix and heading, drawn visibly
  differently from a real fix so it is never mistaken for one
- **Extract guidance** — nearest extract with distance, bearing, and its availability
  conditions
- **Pre-raid briefing** — before the raid starts, from the map-load log event ~30 s
  ahead: which quests are on this map, which keys to bring, which extracts qualify
- **Spawn awareness** — spawn zones and boss spawn chances for the current map
- **Loot layer** — containers and loose loot spawns, toggleable
- **Raid history** — every raid, map, duration and route, from data we already parse
- **Profile-free operation** — everything works with no account linked anywhere

## Explicitly out of scope

Not because they are hard, but because of `adr/0002-no-game-process-interaction.md`:

- Continuous real-time position (would require memory reading)
- Rendering inside the game's frame (would require hooking DirectX)
- Anything reading other players' positions, loot or state
- Any form of input automation

These are permanent exclusions, not deferred items.

## Open questions

- Quest progress source — TarkovTracker API, log-derived, manual, or all three
- How many objectives actually have `zones` populated in the API; this determines how
  much of M2 has real map data behind it
- Whether raster tiles or the layered SVG maps give the better result
- Raid-end detection is currently a stack-trace symbol and may be fragile across
  patches; needs a timeout fallback
