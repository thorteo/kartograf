# Architecture

## Shape of the system

```
        game writes files                  we only ever read
  ┌──────────────────────────┐
  │ Logs/log_*/…             │──watch──▶ LogParser ─────┐
  │   application_000.log    │                          │
  │   output_000.log         │                          │
  ├──────────────────────────┤                          ▼
  │ Screenshots/*.png        │──watch──▶ ShotParser ─▶ EventQueue
  │   (filename only)        │                          │
  └──────────────────────────┘                          │
                                                        ▼
  ┌──────────────────────────┐                    ┌───────────┐
  │ api.tarkov.dev/graphql   │──fetch──▶ Cache ──▶│ RaidState │  single writer,
  │ assets.tarkov.dev tiles  │  (SQLite)          └───────────┘  one mutex
  └──────────────────────────┘                          │
                                                snapshot│(per frame)
                                                        ▼
                                              ┌───────────────────┐
                                              │ ImGui on D3D11 +  │
                                              │ DirectComposition │
                                              └───────────────────┘
                                                        │
                                              layered, click-through,
                                              always-on-top window
```

The game process is never touched. See `adr/0002-no-game-process-interaction.md`.

## The platform boundary

The single most important structural rule, because development happens on Linux and the
target is Windows.

```
src/core/       portable C++20. No <windows.h>, ever. All tests live here.
src/platform/   platform.h declares interfaces; win32/ and stub/ implement them.
src/ui/         ImGui views. Portable — renders against the stub backend on Linux too.
src/app/        entry point, wiring, lifetime.
```

Interfaces that `src/platform/platform.h` declares:

| Interface | win32 | stub (Linux) |
|---|---|---|
| `IDirectoryWatcher` | `ReadDirectoryChangesW` | inotify, or a polling fake |
| `IHttpClient` | WinHTTP | libcurl, or replayed fixtures |
| `IOverlayWindow` | D3D11 + DirectComposition | a plain window, or headless |
| `IHotkeys` | `RegisterHotKey` | no-op |
| `IGameWindowTracker` | `SetWinEventHook` | fake reporting a fixed rect |

Everything interesting — parsing, projection, floor selection, quest filtering, the
raid state machine — sits in `core/` and is unit-testable in WSL without a Windows
build. The Windows-only surface is deliberately thin.

## Modules

### `core/game`

- **`LogParser`** — tails the game's session folder, recognises the five raid lifecycle
  events. Format and pitfalls: `research/eft-log-format.md`. Map ids are matched
  case-insensitively; this is not optional.
- **`ScreenshotParser`** — parses filenames only, never opens image data. Yields
  position, orientation quaternion and in-game time.
  Format: `research/eft-screenshot-format.md`.
- **`RaidState`** — the single piece of shared mutable state. Current map, latest
  position fix and its age, raid phase, active objectives. Owned by one writer,
  published to the renderer as an immutable snapshot.
  See `adr/0004-threading-model.md`.

### `core/geo`

- **`Projection`** — game `(x, y, z)` to image coordinates, using tarkov.dev's
  per-map `transform` and `coordinateRotation`. Watch the negated vertical scale.
- **`Heading`** — orientation quaternion to compass bearing.
- **`FloorSelector`** — picks the map layer from elevation **and** horizontal bounds.
  Both conditions are required.

All of this is pure functions over plain data, which is why the 741 real screenshot
positions in `tests/fixtures/` can validate it directly. See
`research/map-projection.md` for the test that must be written first.

### `core/data`

tarkov.dev GraphQL client, the normalised model, and a SQLite cache. Never refreshes
during a raid. Ships with a bundled snapshot so the first run works offline.
See `research/tarkov-dev-api.md`.

### `ui`

ImGui in **reactive repaint** mode: a frame is drawn only when the `RaidState`
snapshot pointer changes, a displayed second ticks over, or the user is interacting.
Everything else sleeps in `MsgWaitForMultipleObjects`.

This is not a micro-optimisation. Measured on this machine with the HUD on screen:
presenting every vsync cost **9.8% of a core**; the reactive loop costs **0.2%**, with
a 45 MB working set. Those frames would otherwise have come out of the game's budget
for redrawing a panel that had not changed.

Two presentations:

- **HUD** — small, always available, cheap. Raid timer, in-game clock, next objective,
  bearing and distance, age of the last position fix.
- **Map** — fullscreen, opened by hotkey. A fullscreen composited transparent surface
  is the most expensive thing the overlay can do, so it is on demand rather than
  permanent. This is a bigger performance lever than any stack choice.

## Data flow for one position fix

1. Player presses the in-game screenshot key.
2. `IDirectoryWatcher` reports a new file in the screenshots folder.
3. `ScreenshotParser` reads the **name**, producing position, quaternion, in-game time.
4. An immutable event goes on the queue.
5. The single writer applies it to `RaidState` and marks the fix time.
6. The render thread takes a snapshot on its next frame, projects the position through
   `core/geo`, selects the floor, and draws.
7. The fix visibly ages. The UI never implies the marker is live when it is not.
