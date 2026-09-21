# tarkov-help

A Windows overlay for Escape from Tarkov: your live position and your active quest
objectives, drawn on top of the game.

> **Status: early.** The design is settled and documented; the implementation has not
> started. See [docs/features/roadmap.md](docs/features/roadmap.md).

## How it works

The game writes its own position into the filename every time you press the screenshot
key, and logs the current map and raid state to plain text files. This tool reads those
files and draws the result in a transparent window over the game.

**It never touches the game process** — no memory reading, no injection, no DirectX
hooking. That is a hard design constraint, not a current limitation:
[ADR 0002](docs/adr/0002-no-game-process-interaction.md).

## What it will do

- Live position and heading on the correct map, detected automatically from the game's
  own logs
- Automatic floor switching on multi-level maps
- Active quest objectives, with the keys you need to bring
- Raid timer and in-game clock
- Transparent, click-through, rebindable toggle hotkey, excluded from screen capture

## Requirements

- Windows 10 1903 or later, x64
- Escape from Tarkov running in **borderless** or **windowed** mode — an overlay cannot
  be composited over exclusive fullscreen

## Honest limitations

- **Position updates only when you press the screenshot key.** It is a ping, not a
  continuous GPS. The UI always shows how old the last fix is. Getting a live position
  would require reading game memory, which this project will not do.

## Data

Quest, map and item data come from [tarkov.dev](https://tarkov.dev), as do the map
tiles. No game assets are bundled.

## Documentation

[docs/](docs/README.md) — architecture, decision records, and reverse-engineering notes
on the log and screenshot formats.

## Building

Not buildable yet; the toolchain comes first. See [docs/build.md](docs/build.md).
