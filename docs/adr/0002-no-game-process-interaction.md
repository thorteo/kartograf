# 0002. Never interact with the game process

**Status:** Accepted
**Date:** 2026-09-21

## Context

Escape from Tarkov runs BattlEye. The techniques an overlay might reach for — reading
game memory for the player's position, injecting a DLL, hooking `IDXGISwapChain::Present`
to draw inside the game's own frame — are exactly the techniques anti-cheat exists to
detect. Steam's and Discord's overlays work by hooking DirectX; copying that design
here would put the user's account at risk.

Fortunately the game hands us what we need voluntarily:

- Player position and camera orientation, encoded into screenshot filenames
- Current map, raid state and timings, written to plain-text log files

Both are ordinary files on disk, written by the game for its own purposes.

## Decision

The application **never touches the game process**. Specifically, it will not:

- call `OpenProcess`, `ReadProcessMemory` or `WriteProcessMemory` against the game
- inject a DLL by any mechanism
- hook, detour or patch any function in the game
- hook DirectX, Vulkan or the swap chain
- read or modify the game's network traffic
- automate input into the game

It reads files the game writes, and it draws in its own top-level window.

If a proposed feature cannot be built within those limits, the feature is not built.
There is no version of this project where a marginal feature is worth a user's account.

## Options rejected

**Memory reading for continuous position.** This is the only way to get a smooth,
continuously updating position marker, and it is what makes the difference between a
"GPS" and a "ping". Rejected outright. The screenshot-key approach gives a position fix
only when the player asks for one, and that limitation is accepted permanently.

**DirectX hooking for in-frame rendering.** Would render inside the game's frame,
avoiding the DWM compositing cost and working in exclusive fullscreen. Rejected for the
same reason. The cost is that the overlay requires borderless or windowed mode — see
`0003-overlay-window.md`.

## Consequences

- Position is a **ping, not a stream**. The UI must communicate the age of the last fix
  honestly rather than implying live tracking. Dead reckoning from the last known
  heading may be offered, but must be visually distinct from a real fix.
- The overlay does not work in exclusive fullscreen.
- The tool sits in the same category as TarkovMonitor and similar log-reading
  utilities, which are widely used without issue.
- This ADR is a standing constraint on all future work, not a one-time decision.
  Any change touching it needs a superseding ADR and a very good argument.
