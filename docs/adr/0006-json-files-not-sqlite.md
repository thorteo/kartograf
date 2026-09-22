# 0006. Caches and user state as JSON files, not SQLite

**Status:** Accepted
**Date:** 2026-09-22

Supersedes the storage choice in [0001](0001-stack-selection.md), which named the
SQLite amalgamation. SQLite was never vendored; this records what was built instead.

## Context

Everything the overlay stores is one of three kinds:

- **Downloaded documents** — tarkov.dev's tasks, maps and locales per game mode, as
  the JSON API serves them (`research/tarkov-dev-rest-api.md`); map tiles, SVG
  drawings and icons as the CDN serves them.
- **Quest progress** — a few hundred records of quest id, state, time and whether the
  player set it by hand.
- **Settings** — fifteen fields.

Each is read whole at startup or on a map change, and written whole when it changes.
Nothing is queried by anything but its key, nothing needs a transaction across two
records, and there is exactly one writer per file. What does matter: a crash or a full
disk mid-write must not leave a truncated file that is read back as valid forever, and
settings should stay hand-editable.

## Decision

Plain files under `%LOCALAPPDATA%\tarkov-help\`:

- `data/<mode>/` — the API documents byte for byte, plus `tasks.updated` holding the
  Unix time of the download. Parsed on load; a document that parses to nothing is
  deleted so the next start downloads it again.
- `tiles/`, `svg/`, `icons/` — as served, keyed by map, floor, zoom and position.
- `progress.json`, `settings.json` — written by us, stable key order, versioned.

Every write goes through `win32::writeFileAtomic`: to `<name>.part`, then renamed over
the original.

## Options rejected

**SQLite, as 0001 planned.** It would give atomic writes for free and a query layer we
have no queries for. The documents would have to be stored as blobs anyway — they are
re-parsed from the API's own shape, which changes with the API — so the database would
be a container of files. Settings would stop being editable in a text editor. And it
is a large vendored dependency for no capability we use.

**Normalising the API data into our own schema on disk.** Would save re-parsing the
documents at startup (not measured; the maps document is 8.5 MB) at the cost of a
migration every time the API or our model changes. Keeping the documents as served
means a model change needs no migration: the next start parses the same bytes into
the new shape.

## Consequences

- A new kind of stored data needs no infrastructure: a path and `writeFileAtomic`.
- There is no cross-file atomicity. A download writes several documents one by one;
  a crash between them leaves a cache that is new in part. The stamp is written
  last, so such a cache counts as the older download and is refreshed.
- What we rely on is that the rename either replaces the file or leaves the old one,
  never half of each. Power loss can still lose the latest write (there is no flush to
  disk); for a cache, and a progress file rebuilt from logs, that is acceptable.
