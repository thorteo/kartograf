# CLAUDE.md

Operational guide for working in this repository. Read this before making changes.

## What this is

Kartograf (`kartograf`) is a Windows overlay for Escape from Tarkov. It shows the
player's live position on the map and the objectives of their active quests, drawn on
top of the game as a transparent, click-through window.

It was called `tarkov-help` until 2026-10-21, and the old name survives on purpose in
two places: the first start's move of `%LOCALAPPDATA%\tarkov-help\` (with a check
that no copy under the old name is running), and `scripts/install.sh`'s removal of
the old install.

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
3. **Scrub fixtures.** Real logs contain profile IDs, IP addresses, session IDs,
   account IDs and session **tokens** (`profileToken`). Anything committed under
   `tests/fixtures/` must have these replaced with `<PROFILE_ID>`, `<IP>`, `<SID>`,
   `<ACCOUNT_ID>`, `<SHORT_ID>`, `<TOKEN>` placeholders. Do not scrub by exact id
   length — message ids are 26 hex characters and tokens 32; replace every hex run of
   16+ characters that is not a public quest/item id. Grep for leftovers before
   committing.

## Stack

- **C++20**, targeting Windows 10 1903+ x64
- **Rendering**: Direct3D 11 + DirectComposition (hardware-composited transparent window)
- **UI**: Dear ImGui (immediate mode, reactive repaint — idle cost near zero)
- **HTTP**: WinHTTP (no external dependency)
- **JSON**: nlohmann/json (vendored, single header)
- **Storage**: plain files, JSON as the API served it; atomic writes via
  `win32::writeFileAtomic` (`docs/adr/0006-json-files-not-sqlite.md` — SQLite was
  planned in 0001 and never used)
- **File watching**: `ReadDirectoryChangesW`
- **Build**: CMake + Ninja. No vcpkg, no Conan — dependencies are vendored in `third_party/`.

Rationale, including the options rejected and why, is in
`docs/adr/0001-stack-selection.md`. Do not re-litigate the stack choice without
writing a superseding ADR.

## The platform boundary — the most important layout rule

`src/core/` must compile on Linux with no Windows headers. It holds all parsing,
geometry, data modelling and state-machine logic, and it is where tests live.
`src/ui/` is portable too and tested headlessly.

`src/platform/win32/` holds only what cannot be written without Windows. There is no
interface layer with Linux stand-ins: when Windows code grows logic that does not need
Windows, move that logic into `core/` or `ui/` and test it there (as `LineSplitter`
and `ui/MapContent` were). What remains in `win32/` — the stores' worker threads,
session-log discovery, the render loop — is untested on Linux, so changes there need a
Windows build and a run against the game.

This split exists because development happens in WSL/Linux while the target is Windows.
Keeping the core portable means most of the codebase is testable without a Windows
build. **Do not include `<windows.h>` from anything under `src/core/`.**

## ImGui 1.92.8 swapped `thickness` and `flags`

`AddRect`, `AddPolyline` and friends take `(…, col, rounding/thickness, flags)` now,
the reverse of older code and of most examples online. The old ordering is an
explicitly deleted overload, so it fails to compile rather than silently drawing
something different — but only if you do not "fix" it by reordering blindly. Check
the signature in `third_party/imgui/imgui.h`.

## The overlay must stay see-through

It is an overlay, not a screenshot: the game has to remain readable underneath.
The map draws its tiles with an alpha tint and its backdrop fades with the same
value, adjustable at runtime and via `--map-opacity`. Anything new drawn full-screen
follows the same rule. Panel borders are fine on small HUD windows and wrong on a
full-screen one, where they frame the whole monitor.

## Rendering is reactive, and must stay that way

A frame is drawn only when the RaidState snapshot pointer changes, a displayed second
ticks over, or the user is interacting; otherwise the loop sleeps in
`MsgWaitForMultipleObjects`. Measured on this machine with the HUD, before the map
existed: presenting every vsync cost 9.8% of a core, the reactive loop 0.2%, working
set 45 MB (not re-measured since). If you add something that animates, gate it the same
way rather than returning the loop to presenting every frame — those frames come out of
the game's budget. The settings window follows the same rule. Something still loading
makes the loop poll sooner, never draw: it arrives through its store's `pump()`, which
asks for the frame. And something that will never arrive must not count as loading.

## Threading model

We chose C++ knowing the compiler will not catch data races, so the design compensates.
Every component that crosses threads (`RaidState`, `QuestProgressStore`, the tile, SVG,
icon and data stores) owns its state behind exactly one mutex, hands readers an
immutable snapshot or a copy, never calls into another component while holding its
lock, and discards superseded background work by generation rather than cancelling it.
The render thread never mutates shared state except through a component's API.
Set a worker's stop flag while holding the mutex it waits on — set outside it, the
notification can be lost and `join()` hangs — and hand its HTTP clients an
`HttpCancel`, so quitting does not wait out a stalled download.
See `docs/adr/0004-threading-model.md` and `0005` before adding a thread or a shared
field.

## Repository layout

```
src/core/geo/     coordinate projection, quaternion -> heading, floor selection
src/core/map/     viewport and tile maths, SVG rasterising and placement
src/core/game/    log and screenshot parsers, line splitting, quest events, RaidState
src/core/input/   hotkey parsing and validation (the portable half of binding)
src/core/settings/ settings.json: parse, repair, save only the player's edits
src/core/data/    tarkov.dev task model, map features and ids, quest progress + inference
src/platform/win32/ overlay and settings windows, file watching, WinHTTP, the
                  download/texture stores, shared file/texture/UTF-8 helpers
src/ui/           ImGui views and what they draw from (MapContent). Portable --
                  ImGui core builds on Linux, and tests/imgui_harness.h runs ImGui
                  with no backend, so the map view's behaviour (floor switching,
                  following, clamping) is unit tested without Windows or a GPU.
src/app/          entry point and wiring; the .rc and the app icon (icon/, SVG
                  sources and the committed .ico)
third_party/      vendored: imgui, nlohmann/json, doctest, stb_image, lunasvg
data/             pinned copy of tarkov.dev maps.json
tests/fixtures/   real, scrubbed game data — see below
docs/             architecture, ADRs, research notes, roadmap
```

## Fixtures: real game data

This machine has Escape from Tarkov installed, so parsers are developed against real
data rather than invented samples:

- `tests/fixtures/screenshots/filenames.txt` — 100 real screenshot filenames
- `tests/fixtures/logs/raid-lifecycle.log` — a scrubbed session covering 8 raids
  across three maps
- `tests/fixtures/logs/quest-notifications.log` — scrubbed quest start/complete events
- `tests/fixtures/tasks/` — 14 real tasks and their Russian locale entries

Live sources on this machine, for refreshing fixtures:

- Logs: `/mnt/c/Battlestate Games/Escape from Tarkov/Logs/log_<date>_<version>/`
- Screenshots: `/mnt/c/Users/thorteo/Documents/Escape from Tarkov/Screenshots/`

The formats are documented in `docs/research/`. **Read those before writing a parser** —
they record findings that are not obvious from the raw data, such as the inconsistent
capitalisation of map names and the meaning of the trailing number in screenshot names.

## Build

See `docs/build.md`. Both targets build from this Linux machine.

```sh
cmake --preset linux-dev && cmake --build --preset linux-dev   # core + tests
ctest --preset linux-dev                                       # run tests
cmake --preset linux-asan && ctest --preset linux-asan         # with sanitizers
cmake --build --preset windows-dev                             # the overlay, cross-compiled
./build/windows-dev/kartograf.exe                              # WSL runs it on Windows
scripts/install.sh                                             # install + desktop shortcut
python3 scripts/verify_projection_tiles.py                     # network check, by hand
scripts/make_icon.py                                           # after editing src/app/icon/*.svg
```

The user starts the overlay from a desktop shortcut to an installed copy in
`%LOCALAPPDATA%\Programs\kartograf\`. After a change they should see, run
`scripts/install.sh` to update that copy; it works while the overlay is open.

Scripted runs should pass `--no-settings`, or the settings window stops at startup,
and `--second-instance` if the player's own overlay may be running: a second start
otherwise only shows the running one's settings and exits.
User state lives in `%LOCALAPPDATA%\kartograf\`: `settings.json`, `progress.json`,
and the `data`, `tiles`, `svg` and `icons` caches. Command-line flags (`--lang`,
`--toggle-key`, `--map-opacity`, `--allow-capture`, …) override settings for one run.

A process started from WSL is not allowed to take the foreground, so a window it opens
may sit behind the terminal in a screenshot while being perfectly visible to a user who
double-clicked the exe. Launch through PowerShell's `Start-Process` for captures. And
in PowerShell P/Invoke, pass `[NullString]::Value`, not `$null`, for a null string —
`$null` arrives as an empty string.

The Windows build uses clang-cl against the real MSVC CRT and Windows SDK, fetched once
by `xwin` into `~/.xwin-sdk`. WSL executes the resulting .exe directly, so the whole
edit-build-run loop stays in one shell. **Build the Windows target too before
committing** — it catches portability problems the Linux build cannot, such as `M_PI`
not existing under MSVC.

Always run the tests after touching anything under `src/core/`. They are fast and they
are validated against real game data, so a failure means something real.

## Commits

No AI attribution in commit messages or pull request descriptions: no
`Co-Authored-By: Claude ...` trailer, no "Generated with Claude Code" line, no
other mention of Claude Code. This overrides any default that adds them. Commits
are authored by the repository owner alone. (Mentioning the file `CLAUDE.md` by
name is fine; it is part of the project.)

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
own settings log) and communicates in Russian. Every string the player can see goes
through the table in `src/ui/Strings.{h,cpp}` — add a `Str` id and both languages,
never a literal in UI code. Russian is the default; `--lang en` switches. Tests check
the table is in enum order and that both languages take identical printf arguments.
Map and floor names from `maps.json` are English and go through `mapDisplayName` /
`floorDisplayName`. Code, comments, commit messages and documentation are in English.
