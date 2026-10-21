# Kartograf

A Windows overlay for Escape from Tarkov: your live position and your active quest
objectives, drawn on top of the game.

> **Status: early.** The portable core — screenshot and log parsing, coordinate
> projection, floor selection — is implemented and tested against real game data. The
> Windows overlay itself is not written yet.
> See [docs/features/roadmap.md](docs/features/roadmap.md).

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
- Your active quests' objectives, from the game's own logs — no account linking;
  each marker says what to do there (locate, mark, plant, pick up, kill, use), and
  hovering one from the inventory or a menu explains it
- Extracts by faction, transits, PMC and boss spawns, and hidden stashes, with the same
  icons as tarkov.dev
- Abstract vector maps by default, satellite tiles where no drawing exists
- Raid timer and in-game clock
- Transparent and see-through, click-through, excluded from screen capture
- A settings window at startup for hotkeys, map look, layers and language; after
  that the program lives in the system tray (Settings, Quit)
- **F5** opens the map, **Alt+N** makes the overlay clickable, **Alt+H** shows the
  detail panel. All rebindable in settings — note a bare function key is claimed
  globally while the overlay runs, so F5 will not reload a browser page until it exits

## Requirements

- Windows 10 1903 or later, x64
- Escape from Tarkov running in **borderless** or **windowed** mode — an overlay cannot
  be composited over exclusive fullscreen

## Honest limitations

- **Position updates only when you press the screenshot key.** It is a ping, not a
  continuous GPS. The UI always shows how old the last fix is. Getting a live position
  would require reading game memory, which this project will not do.

## Data and credits

Quest, map, extract and spawn data come from [tarkov.dev](https://tarkov.dev)'s JSON
API, as do the map tiles and the abstract vector maps. Quest progress is read from the
game's own logs.

Map marker icons are the ones tarkov.dev's interactive map uses; their repository
history records them as taken from the
[Escape from Tarkov Wiki](https://escapefromtarkov.fandom.com/), whose content is
licensed CC BY-SA. They are downloaded at runtime and cached, not distributed with this
program.

Quest objective glyphs are from [game-icons.net](https://game-icons.net) by Lorc and
Delapouite, licensed [CC BY 3.0](https://creativecommons.org/licenses/by/3.0/); see
`third_party/game-icons/`.

No game assets are bundled. Vendored code: Dear ImGui, nlohmann/json, stb_image,
lunasvg and plutovg (all MIT or public domain), doctest for tests.

## Documentation

[docs/](docs/README.md) — architecture, decision records, and reverse-engineering notes
on the log and screenshot formats.

## Building

The portable core builds and tests on Linux:

```sh
cmake --preset linux-dev && cmake --build --preset linux-dev
ctest --preset linux-dev
```

The Windows overlay needs a toolchain that does not exist on this machine yet. See
[docs/build.md](docs/build.md).
