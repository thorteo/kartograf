# 0001. Language, UI and rendering stack

**Status:** Accepted; storage superseded by [0006](0006-json-files-not-sqlite.md)
**Date:** 2026-09-21

## Context

A fullscreen transparent overlay that runs alongside Escape from Tarkov. Requirements
that shaped the choice:

- Runs for hours next to a game that uses 12–16 GB of RAM and saturates the GPU.
- Mostly static content: position updates arrive only when the player presses the
  screenshot key, perhaps once a minute. This is not a high-framerate application.
- Needs Win32 window control: layered transparent window, click-through toggle,
  never steal focus, a **rebindable** global hotkey, and exclusion from screen capture
  so the overlay does not appear in recordings or streams.
- Needs a zoomable tile map with a few thousand markers.
- Needs HTTP + JSON against tarkov.dev, a local cache, and directory watching.
- Development happens in WSL/Linux; the target is Windows.

An important framing point established during evaluation: the dominant cost of an
overlay is not the UI toolkit's drawing speed. It is (1) repainting when idle and
(2) DWM compositing a fullscreen transparent surface. The second is paid identically by
every stack. The first is where browser-based stacks genuinely lose.

The largest performance lever is not the stack at all — it is keeping a small HUD
permanently visible and showing the fullscreen map only on demand.

## Decision

**C++20 + Dear ImGui + Direct3D 11 + DirectComposition**, built with CMake + Ninja,
dependencies vendored in `third_party/` with no package manager.

Supporting choices: WinHTTP for HTTP (no dependency), nlohmann/json, SQLite
amalgamation, `ReadDirectoryChangesW` for watching.

## Options rejected

**Electron + React.** Rejected on idle cost and footprint: 250–400 MB RAM, ~120 MB
per update download, and a compositor that wakes continuously for content that rarely
changes. The common objection that Electron cannot do the Win32 work turned out to be
false — `setIgnoreMouseEvents`, `focusable: false`, `setContentProtection` and
`globalShortcut` cover most of it, and `electron-overlay-window` handles attaching to
the game window. It was rejected on cost, not capability. Its one real structural
limit is that `electron-overlay-window` supports exactly one overlay window and cannot
be re-initialised.

**Tauri v2 + React.** The initial recommendation, and a close call. Better than
Electron on every axis that mattered (~8 MB updates, a Rust core that can run without a
window at ~10 MB). Rejected because it still carries a WebView2 compositor for content
that is nearly static, splits the codebase across two languages with IPC serialisation
between them, and has less proven practice for fullscreen transparent always-on-top
surfaces over a DirectX game than the native path.

**Rust + egui/wgpu.** Genuinely strong: one language throughout, `with_transparent` /
`with_always_on_top` / `with_mouse_passthrough` available, reactive repaint that costs
nothing when idle, ~40–60 MB, cross-compiles from WSL via `cargo xwin`, and a borrow
checker that eliminates the data races this design is exposed to. Rejected in favour of
C++ because every DirectComposition, D3D11 and layered-window reference is written in
C++, and the overlay layer is the least certain part of the project — the place where
being able to follow existing samples directly is worth the most. Dear ImGui is also
more mature than egui for this specific use.

The argument that C++ requires heavy dependency plumbing was investigated and dropped:
SQLite is a single amalgamation file, nlohmann/json is a single header, ImGui is a
handful of files, WinHTTP and `ReadDirectoryChangesW` are part of the platform. No
package manager is needed. Setup is a day, not a week.

**C# + WPF/WinUI or Avalonia.** WPF gives the best Win32 ergonomics via P/Invoke but
cannot be built on Linux at all, which removes the ability to develop any part of the
application from the WSL environment. Avalonia does build on Linux and was the fallback
had C++ been declined. Both would still have rendered the map through a separate
engine, so neither avoided the real work.

## Consequences

**We accept, knowingly:** the compiler will not catch data races, and this application
is inherently multithreaded — watcher threads feeding a render thread is its core
shape. This is the one genuine advantage Rust held, and it is mitigated by design
rather than by the type system. See `0004-threading-model.md`. That mitigation is not
optional; it is the price of this decision.

**We accept:** the map viewport is ours to write — tile pyramid, LOD, async fetch,
texture cache, pan/zoom, marker culling and hit-testing. Estimated at a few days. With
Leaflet this would have been free.

**We gain:** the smallest footprint of any option considered (~25–40 MB), effectively
zero CPU when idle, direct use of the entire body of Win32 overlay reference material,
and a single-binary distribution with no runtime prerequisites.

**Watch out for:** MSVC is not installed on this machine, and CMake and Clang are not
present on the Linux side. See `docs/build.md` — the toolchain has to be established
before any of this compiles.
