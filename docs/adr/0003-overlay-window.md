# 0003. Overlay via DirectComposition, not a DirectX hook

**Status:** Accepted
**Date:** 2026-09-21

## Context

We need a transparent, always-on-top, optionally click-through window over the game.
`0002` rules out drawing inside the game's frame, so the overlay must be our own
top-level window.

Two ways to get a hardware-accelerated transparent top-level window on Windows:

- `UpdateLayeredWindow` with a CPU-side bitmap — simple, but every frame round-trips
  through system memory.
- **DirectComposition** with `CreateSwapChainForComposition` — the compositor handles
  the transparent surface on the GPU. This is the mechanism Windows itself uses.

## Decision

A borderless top-level window with
`WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW`, rendered
through Direct3D 11 into a DirectComposition swap chain.

- `WS_EX_NOACTIVATE` — the overlay must never take focus from the game.
- `WS_EX_TOOLWINDOW` — keeps it out of the taskbar and Alt-Tab.
- `WS_EX_TRANSPARENT` — toggled at runtime for click-through. Cleared when the user
  wants to interact with the map, set again afterwards.
- `SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE)` — keeps the overlay out of
  screen recordings and streams. Exposed as a setting, on by default.

Window placement follows the game window rather than the monitor: locate it by class
and title, track it with `SetWinEventHook` on `EVENT_OBJECT_LOCATIONCHANGE` and
`EVENT_SYSTEM_FOREGROUND`, and hide the overlay whenever the game is not foreground.
Polling `GetForegroundWindow` is the fallback if the hook proves unreliable.

The toggle hotkey is registered with `RegisterHotKey` and **must be rebindable** from
the settings UI, including modifier combinations, with the binding persisted and a
clear indication when a chosen combination is already claimed by another application.

## Options rejected

**`UpdateLayeredWindow`.** Adequate for a static HUD, wasteful for a pannable map.
Kept in mind as a fallback if DirectComposition misbehaves on some configuration.

**Fullscreen surface always.** Rejected as the default. A permanently composited
fullscreen transparent window is the single largest performance cost the overlay can
impose. Default to a small HUD; expand to the full map only on the hotkey.

## Consequences

- **The game must run in borderless or windowed mode.** In exclusive fullscreen nothing
  will be drawn. This has to be stated plainly in the README and detected at runtime,
  with a clear message, rather than appearing as "the overlay is broken".
- Multi-monitor and DPI changes need explicit handling; the overlay must re-anchor when
  the game window moves between monitors.
- `WDA_EXCLUDEFROMCAPTURE` requires Windows 10 version 2004 or later. On older builds
  fall back to `WDA_MONITOR` and say so in the settings UI.
