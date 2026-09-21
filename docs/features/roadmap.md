# Roadmap

The stated goal is to be better than TarkovQuestie, not merely equivalent. This file
records what that means concretely, in the order we intend to earn it.

## M0 — Foundations  ✅

- [x] Linux toolchain installed; `core/` builds and tests with no Windows headers
- [x] **Projection validated against real positions.** 258 screenshots correlated with
      the log's raid windows to label each with its map; all fall inside their map's
      bounds, and 110 distinct projected tile indices all resolve on the CDN. This also
      settled the open question about `bounds` semantics — see
      `research/map-projection.md`.
- [x] Log parser recognises all five lifecycle events across the fixture's 8 raids
- [x] Screenshot filename parser handles the full corpus, including multi-digit `(N)`
      suffixes and Russian-locale decimal hazards
- [x] Floor selection from elevation *and* horizontal region
- [x] Windows toolchain: clang-cl + xwin cross-compiling from WSL, verified by
      running the result on Windows through interop
- [x] Transparent DirectComposition overlay that initialises on the real GPU

30 test cases, 5971 assertions, all passing.

## M1 — The GPS core

- [x] Transparent click-through overlay: D3D11 + DirectComposition, `WS_EX_LAYERED |
      WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW`, verified initialising
      on this machine's GPU
- [x] **Rebindable toggle hotkey** with conflict detection. Parsing and validation are
      portable and unit-tested; registration is Win32. Confirmed live: `Ctrl+Shift+M`
      is already claimed on this machine and is reported as such, while `Alt+M`,
      `Ctrl+Alt+F9` and `F8` bind cleanly.
- [x] Excluded from screen capture — `WDA_EXCLUDEFROMCAPTURE` confirmed working here
- [x] Overlay follows the game window and hides when the game is not foreground
- [x] HUD with map, raid phase, elapsed time, position, heading, elevation and
      in-game clock. **Off by default**: almost all of it is either visible on the
      map, already shown by the game, or a raw coordinate nobody navigates by, and
      a permanent panel costs a screen corner that matters more in a raid. `Alt+H`
      or `--hud` brings it back. The one thing it uniquely carried — the age of the
      position fix — moved to the map footer, coloured by confidence
- [x] Automatic map detection from logs, with the game install located through the
      registry rather than by inspecting the running process
- [x] Live position from the screenshots folder via `ReadDirectoryChangesW`
- [x] **Reactive rendering.** Measured on this machine: continuous presentation cost
      9.8% of a core; drawing only when the state object changes, a displayed second
      ticks, or the user is interacting costs **0.2%**. Working set 45 MB.
- [ ] Hotkey binding persisted to a config file (currently a command-line flag)
- [x] **The map view.** Raster tiles from the same CDN tarkov.dev uses, fetched by a
      worker pool, cached to disk forever, decoded with stb_image and uploaded to the
      GPU on the render thread with an LRU texture budget of 192 tiles (~48 MB).
      Drag to pan, wheel to zoom about the cursor, follow-the-player by default.
- [x] **Marker verified against the map, not just the map bounds.** Injecting a real
      Customs position put the marker on the south-west corner of Dorms, at the tree
      line, with the heading arrow pointing the right way. This was the last thing the
      earlier tests could not confirm.
- [x] maps.json compiled into the binary, so a release stays one file
- [x] **Automatic floor switching.** The view draws ground, then the active floor
      over it, picked from the player's elevation *and* horizontal position. Verified
      with a real Dorms position on Customs: y 4.43 selects 2nd Floor, y 0.50 the
      ground, y 9.00 the 3rd, and the same elevation out in the open stays ground.
      A manual override with an auto toggle is available while interactive.
- [x] The map fills the screen when centred on the player, rather than letterboxing
      or leaving the map's edge inside the viewport
- [x] The map is see-through, with adjustable opacity (`--map-opacity`, a slider,
      and Ctrl+wheel), and no window border framing the monitor
- [ ] Quest objectives on the map
- [ ] Automatic map detection from logs — the overlay opens the right map by itself
- [ ] Live position marker with heading, and a visible **age indicator** on the fix
- [ ] Automatic floor switching from elevation plus bounds
- [ ] Raid timer and in-game clock

## M2 — Quests

- [ ] tarkov.dev task data cached locally, bundled snapshot for offline first run
- [ ] Objective markers on the map, filtered to the current map
- [ ] Active-quest list with distance and bearing to each objective
- [ ] **Required keys surfaced per objective** — which key to bring, before the raid
- [x] **Quest progress from the game's own logs.** Starts and completions are read
      from `push-notifications_000.log`, imported from every session still on disk,
      persisted (the game keeps ~5 days), and extended by inference through quest
      prerequisites. Markers show only active quests; a panel allows corrections.
- [x] Extracts (by faction), transits, PMC and boss spawns, with icons
- [x] UI in Russian by default through a string table, English via `--lang en`
- [x] Extract and transit names drawn on the map, with overlap culling

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

## Map rendering

Abstract vector drawings are the default, with the photographic tiles as a
fallback. Neither format covers every map, so both renderers are kept and the
choice is per map. The footer says which one is in use.

The abstract renderer draws only the region on screen, at screen resolution, on a
worker thread. That keeps it sharp at any zoom for the cost of one texture, where a
full-map texture sharp enough to zoom into would cost hundreds of megabytes.

## Known gap: three maps have no raster tiles

Streets of Tarkov, Lighthouse and Terminal publish no tile pyramid upstream — only
SVG. Probing the CDN confirms the tiles genuinely do not exist rather than being
absent from `maps.json`. Interchange and Shoreline draw at ground level but their
upper floors are SVG-only too.

**Closed.** Those three maps are exactly the ones with abstract drawings, so the SVG
renderer covers them. The inverse gap remains and is covered the other way round:
Icebreaker, The Lab and The Labyrinth have no drawing and fall back to tiles.
`tests/test_tile_math.cpp` encodes the current coverage of both so a change upstream
in either direction surfaces.

## Open questions

- Whether to add TarkovTracker sync on top of log-derived progress
- How many objectives actually have `zones` populated in the API; this determines how
  much of M2 has real map data behind it
- Whether raster tiles or the layered SVG maps give the better result
- Raid-end detection is currently a stack-trace symbol and may be fragile across
  patches; needs a timeout fallback
