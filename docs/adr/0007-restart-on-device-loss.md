# 0007. Recover from a lost GPU device by restarting the process

**Status:** Accepted
**Date:** 2026-09-22

## Context

A Direct3D device can be removed under a running program: a driver update, a driver
crash, a TDR, the GPU hanging under a game that pushes it hard. Everything created on
it is dead, and nothing drawn appears again. Before this, the overlay stayed blank
until the player noticed and restarted it — likely mid-raid, where noticing costs
the most.

What lives on the device: the overlay's swap chain, render target and DirectComposition
visual; the settings window's swap chain; two ImGui DX11 backends with their font
atlases; and textures in four stores — tiles, the SVG render, icons and glyphs — each
owned by its store's render-thread side (ADR 0005).

## Decision

When `GetDeviceRemovedReason` reports the device gone, the main loop stops, shuts
everything down as on a normal exit, and starts a new copy of the executable with the
same command line plus `--no-settings --after-device-loss`. The new copy says in its
status line why it restarted. A second loss within a minute of such a restart is
taken as a broken driver, and the program exits instead of restarting again.

## Options rejected

**Rebuild in place.** Recreate the device, then every object on it: both windows'
swap chains, both ImGui backends, the composition tree, and a reset path in every
store that drops textures while keeping decoded pixels or re-decoding from the disk
cache. That is a second initialisation path through most of the Windows layer, run
only when a driver fails — code that would rot untested. Everything worth keeping is
already persisted: settings and progress on every change, downloads in the cache.
What a restart loses is the map's zoom and pan and the status history.

**Do nothing but tell the player.** Cheaper still, but the one thing a blank overlay
cannot do is show a message.

## Consequences

- Recovery takes a second or two: a new process, a new device, caches read from disk.
- Anything added later that holds state only in memory is lost on a device loss. If
  that ever matters, persist it; do not add in-place recovery for it.
- The restart runs the same binary with the same arguments, so development flags such
  as `--map` or `--no-game` survive it.
