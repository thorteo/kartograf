# Documentation

Start here. This index is the map of everything written down; add new documents to it.

## Orientation

| Document | What it answers |
|---|---|
| [architecture.md](architecture.md) | How the pieces fit, and where the platform boundary runs |
| [build.md](build.md) | Toolchain, the two routes to a Windows binary, current machine state |
| [features/roadmap.md](features/roadmap.md) | What we are building, in order, and what is out of scope |

## Decisions — `adr/`

Why the code is the way it is. [Index and process](adr/README.md).

| # | Title |
|---|---|
| [0001](adr/0001-stack-selection.md) | Language, UI and rendering stack |
| [0002](adr/0002-no-game-process-interaction.md) | Never interact with the game process |
| [0003](adr/0003-overlay-window.md) | Overlay via DirectComposition, not a DirectX hook |
| [0004](adr/0004-threading-model.md) | Single guarded state, snapshot to renderer |
| [0005](adr/0005-per-component-snapshot-stores.md) | Per-component snapshot stores |

## Research — `research/`

Reverse-engineering notes on formats we do not control. **Read the relevant one before
writing a parser against it** — these record findings that are not visible in the raw
data, and they say how each finding was verified so a reader can separate fact from
inference.

| Document | Covers |
|---|---|
| [eft-screenshot-format.md](research/eft-screenshot-format.md) | Position, orientation and in-game time from screenshot filenames |
| [eft-log-format.md](research/eft-log-format.md) | Log layout, the five raid lifecycle events, map id quirks |
| [map-projection.md](research/map-projection.md) | Game coordinates to image coordinates, floor selection |
| [tarkov-dev-api.md](research/tarkov-dev-api.md) | GraphQL schema, which objectives carry positions, caching |
| [tarkov-dev-rest-api.md](research/tarkov-dev-rest-api.md) | The JSON API actually used: tasks, locales, map ids |

## Conventions

- Decisions go in `adr/`, never inline in code comments. Accepted records are not
  rewritten — supersede them.
- Findings about external formats go in `research/`, with the verification method
  stated.
- Feature plans go in `features/`.
- Anything committed to `tests/fixtures/` must be scrubbed of profile IDs, IP
  addresses, session IDs and account IDs.
