# CLAUDE.md

Operational guide for working in this repository. Read this before making changes.

## What this is

`tarkov-help` is a Windows overlay for Escape from Tarkov. It shows the player's live
position on the map and the objectives of their active quests, drawn on top of the game
as a transparent, click-through window.

It is comparable to TarkovQuestie / TarkovMonitor in purpose, and deliberately more
capable — see `docs/features/roadmap.md`.

## Hard constraints — do not violate these

1. **Never interact with the game process.** No memory reads, no DLL injection, no
   DirectX/Vulkan hooking, no `OpenProcess`, no `ReadProcessMemory`, no function
   detours. The overlay is our own top-level window. We read only files the game
   writes to disk. This is what keeps the tool safe to run under BattlEye, and it is
   non-negotiable. If a feature seems to require touching the game process, the answer
   is that we do not ship that feature. See `docs/adr/0002-no-game-process-interaction.md`.
2. **Never bundle scraped game assets.** Map tiles and item images are fetched at
   runtime from their published CDN and cached locally.
3. **Scrub fixtures.** Real logs contain profile IDs, IP addresses, session IDs and
   account IDs. Anything committed under `tests/fixtures/` must have these replaced
   with `<PROFILE_ID>`, `<IP>`, `<SID>`, `<ACCOUNT_ID>`, `<SHORT_ID>` placeholders.
   Verify before committing.

## Stack

- **C++20**, targeting Windows 10 1903+ x64
- **Rendering**: Direct3D 11 + DirectComposition (hardware-composited transparent window)
- **UI**: Dear ImGui (immediate mode, reactive repaint — idle cost near zero)
- **HTTP**: WinHTTP (no external dependency)
- **JSON**: nlohmann/json (vendored, single header)
- **Storage**: SQLite amalgamation (vendored)
- **File watching**: `ReadDirectoryChangesW`
- **Build**: CMake + Ninja. No vcpkg, no Conan — dependencies are vendored in `third_party/`.

Rationale, including the options rejected and why, is in
`docs/adr/0001-stack-selection.md`. Do not re-litigate the stack choice without
writing a superseding ADR.

## The platform boundary — the most important layout rule

`src/core/` must compile on Linux with no Windows headers. It holds all parsing,
geometry, data modelling and state-machine logic, and it is where tests live.

`src/platform/` holds everything Windows-specific behind interfaces declared in
`src/platform/platform.h`. `src/platform/stub/` provides Linux implementations so the
core can be built and tested on the development machine.

This split exists because development happens in WSL/Linux while the target is Windows.
Keeping the core portable means most of the codebase is testable without a Windows
build. **Do not include `<windows.h>` from anything under `src/core/`.**

## Threading model

We chose C++ knowing the compiler will not catch data races, so the design compensates:
shared mutable state lives in exactly one place, `core/game/RaidState`, guarded by one
mutex, and is only ever published as an immutable snapshot to the render thread.
Watcher threads push events through a queue; they never touch render state directly.
See `docs/adr/0004-threading-model.md` before adding a thread or a shared field.

## Repository layout

```
src/core/geo/     coordinate projection, quaternion -> heading, floor selection
src/core/game/    log parser, screenshot parser, raid state machine
src/core/data/    tarkov.dev models, GraphQL queries, local cache
src/core/util/    small shared helpers
src/platform/     platform.h interfaces + win32/ and stub/ implementations
src/ui/           ImGui views (map, HUD, settings)
src/app/          entry point and wiring
tests/fixtures/   real, scrubbed game data — see below
docs/             architecture, ADRs, research notes, roadmap
```

## Fixtures: real game data

This machine has Escape from Tarkov installed, so parsers are developed against real
data rather than invented samples:

- `tests/fixtures/screenshots/filenames.txt` — 100 real screenshot filenames
- `tests/fixtures/logs/raid-lifecycle.log` — a scrubbed session covering 8 raids
  across three maps

Live sources on this machine, for refreshing fixtures:

- Logs: `/mnt/c/Battlestate Games/Escape from Tarkov/Logs/log_<date>_<version>/`
- Screenshots: `/mnt/c/Users/thorteo/Documents/Escape from Tarkov/Screenshots/`

The formats are documented in `docs/research/`. **Read those before writing a parser** —
they record findings that are not obvious from the raw data, such as the inconsistent
capitalisation of map names and the meaning of the trailing number in screenshot names.

## Build

See `docs/build.md`. The Linux core build works today; the Windows target has no
toolchain on this machine yet.

```sh
cmake --preset linux-dev && cmake --build --preset linux-dev   # core + tests
ctest --preset linux-dev                                       # run tests
cmake --preset linux-asan && ctest --preset linux-asan         # with sanitizers
python3 scripts/verify_projection_tiles.py                     # network check, by hand
```

Always run the tests after touching anything under `src/core/`. They are fast and they
are validated against real game data, so a failure means something real.

## Documentation conventions

Documentation is meant to be extended, not rewritten. Follow the existing shape:

- **Architectural decisions** go in `docs/adr/` as numbered records. Never edit an
  accepted ADR to change its decision — write a new one that supersedes it and update
  the old one's status line.
- **Findings about game formats or third-party APIs** go in `docs/research/`. These are
  reverse-engineering notes; record how a finding was verified, so a future reader can
  tell fact from inference.
- **Feature plans** go in `docs/features/`.
- `docs/README.md` is the index. Add new documents to it.

## Language

The user plays the game in Russian (confirmed from `"Language": "ru"` in the game's
own settings log) and communicates in Russian. UI strings go through the i18n table in
`src/ui/strings`; Russian is the default locale, English is kept in sync. Code,
comments, commit messages and documentation are in English.
