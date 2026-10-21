# Architecture

## Shape of the system

```
        game writes files                  we only ever read
  ┌──────────────────────────┐
  │ Logs/log_*/…             │──watch──▶ LogParser ──────┐
  │   application_000.log    │           QuestEventParser│──▶ QuestProgressStore
  │   output_000.log         │                           │     (progress.json)
  │   push-notifications_…   │                           ▼
  ├──────────────────────────┤                     ┌───────────┐
  │ Screenshots/*.png        │──watch──▶ parse ───▶│ RaidState │  single writer,
  │   (filename only)        │           name      └───────────┘  one mutex
  └──────────────────────────┘                           │
                                                 snapshot│(per frame drawn)
  ┌──────────────────────────┐                           ▼
  │ json.tarkov.dev          │──fetch──▶ JSON files ─▶ stores ──▶ ImGui on D3D11 +
  │ assets.tarkov.dev tiles, │  (disk cache,         (one lock     DirectComposition
  │   SVG drawings, icons    │   ADR 0006)            each)            │
  └──────────────────────────┘                                  layered, click-through,
                                                                always-on-top window
```

The game process is never touched. See `adr/0002-no-game-process-interaction.md`.
The watcher thread applies events to `RaidState` and `QuestProgressStore` directly;
there is no queue between them.

## The platform boundary

The single most important structural rule, because development happens on Linux and the
target is Windows.

```
src/core/       portable C++20. No <windows.h>, ever. Parsing, geometry, the data
                model, state machines -- and the tests.
src/ui/         ImGui views. Portable: ImGui's core builds on Linux, and
                tests/imgui_harness.h runs it with no backend, so the map view's
                behaviour is tested without Windows or a GPU.
src/platform/   win32/ only: windows, D3D11, file watching, WinHTTP, the stores
                that own GPU textures.
src/app/        main.cpp: wiring and the render loop. kartograf.rc and
                icon/: the app icon, rendered from SVG by scripts/make_icon.py.
```

There is no interface layer with Linux stand-ins. The split is instead by what the
code needs: anything that can be written without Windows goes in `core/` or `ui/`
and is tested there, and `platform/win32/` keeps only what cannot. When a piece of
Windows code grows logic that does not need Windows, that logic moves down -- line
splitting left `LogTail` for `core/game/LineSplitter`, and building the map's markers
left `main.cpp` for `ui/MapContent` -- so it can be tested.

What still lives only in the Windows layer, untested on Linux: the stores' worker
threads and their retry and cancellation paths, the session-log discovery in
`GameDataWatcher`, and the render loop's redraw decisions. Changes there need a
Windows build and a run against the real game.

## Modules

### `core/game`

- **`LogParser`** — recognises the raid lifecycle events in the session logs. Format
  and pitfalls: `research/eft-log-format.md`. Map ids are matched case-insensitively;
  this is not optional.
- **`QuestEventParser`** — quest started, completed and failed, from the trader
  messages in `push-notifications_000.log`.
- **`LineSplitter`** — complete lines out of a file read while the game appends to it.
- **`ScreenshotName`** — parses filenames only, never opens image data. Yields
  position, orientation quaternion and in-game time.
  Format: `research/eft-screenshot-format.md`.
- **`RaidState`** — current map, raid phase and mode, latest position fix and its age.
  Owned by one writer, published to the renderer as an immutable snapshot.
  See `adr/0004-threading-model.md`.

### `core/geo` and `core/map`

- **`Projection`** — game `(x, y, z)` to image coordinates, using tarkov.dev's
  per-map `transform` and `coordinateRotation`. Watch the negated vertical scale.
- **`Heading`** — orientation quaternion to compass bearing.
- **`selectLayer`** (`Projection.h`) — picks the floor from elevation **and**
  horizontal bounds. Both conditions are required.
- **`MapConfig`** — the pinned `data/maps.json`, compiled in.
- **`TileMath`** — viewport, zoom level and the visible tiles, inside the pyramid.
- **`SvgRenderer`**, **`SvgPlacement`** — rasterising regions of tarkov.dev's
  abstract drawings with lunasvg, and where the drawing sits on the map.

All of this is pure functions over plain data, which is why the real screenshot
positions in `tests/fixtures/` can validate it directly. See
`research/map-projection.md`.

### `core/data`

- **`TaskModel`** — tasks, objectives, zones, required items, from the JSON API's
  `/tasks` document and its locales (`research/tarkov-dev-rest-api.md`).
- **`MapFeatures`** — extracts, spawns, transits and hidden stashes from `/maps`.
- **`MapIds`** — BSG map ids to the map each one is drawn as.
- **`QuestProgress`** — the player's progress: log events, the player's own
  corrections, and prerequisites inferred from both.

The downloads themselves are kept by `platform/win32/TarkovDataRepository`, as the
files the API served (`adr/0006-json-files-not-sqlite.md`). Routine refreshes wait
for the raid to end; data that is missing outright is fetched at once.

### `core/input`, `core/settings`

Hotkey text and validation, and `settings.json`: parsing, repair, and keeping
command-line overrides out of what is saved.

### `ui`

ImGui in **reactive repaint** mode: a frame is drawn only when the `RaidState`
snapshot pointer changes, a displayed second ticks over, or the user is interacting.
Everything else sleeps in `MsgWaitForMultipleObjects`.

This is not a micro-optimisation. Measured on this machine with the HUD on screen:
presenting every vsync cost **9.8% of a core**; the reactive loop costs **0.2%**, with
a 45 MB working set. Those frames would otherwise have come out of the game's budget
for redrawing a panel that had not changed. That was before the map existed; it has
not been re-measured with the map, the SVG renderer and the settings window.

The same rule covers everything that draws: the settings window draws after input and
once a second for its status, and pieces of the map still downloading make the loop
poll sooner, not draw -- each arrives through its store's `pump()`, which asks for the
frame itself. Something that never arrives, like a tile the CDN does not have, must not
count as waiting, or the loop presents every frame for as long as the map is open.

Two presentations:

- **HUD** — small and cheap, off by default: raid timer, in-game clock, the age of
  the last position fix. Most of it is on the map footer or in the game itself.
- **Map** — fullscreen, opened by hotkey. A fullscreen composited transparent surface
  is the most expensive thing the overlay can do, so it is on demand rather than
  permanent. This is a bigger performance lever than any stack choice.

## Data flow for one position fix

1. Player presses the in-game screenshot key.
2. `GameDataWatcher`'s `ReadDirectoryChangesW` watch reports the new file, several
   times over while the game writes it; the repeats are dropped.
3. `parseScreenshotName` reads the **name**, producing position, quaternion, in-game
   time.
4. The watcher thread, the single writer, applies it to `RaidState` and marks the fix
   time. The snapshot pointer changes.
5. The render loop sees the new pointer, draws a frame, projects the position through
   `core/geo`, selects the floor, and draws.
6. The fix visibly ages. The UI never implies the marker is live when it is not.
