# Escape from Tarkov log format and raid lifecycle

**Status:** verified against real logs on this machine, game version `1.1.5.1.47510`
(2026-09-16 … 2026-09-21). Fixture: `tests/fixtures/logs/raid-lifecycle.log`.

Screenshots give us *where* the player is. Logs give us *what map they are on* and
*whether a raid is running* — which is what lets the overlay open the right map by
itself instead of asking.

## Location

```
C:\Battlestate Games\Escape from Tarkov\Logs\log_<YYYY.MM.DD>_<H-MM-SS>_<version>\
```

Note this is the **game install directory**, not `%LOCALAPPDATA%Low`. `LocalLow` holds
only `Player.log` and a Vuplex webview cache, neither of which is useful to us. A new
folder is created per game launch.

**The hour is not zero-padded**, so folder names do not sort as strings:
`log_2026.09.21_2-31-42_…` sorts after `log_2026.09.21_19-54-34_…`. Month, day,
minutes and seconds are padded in every folder seen. Picking the newest folder by
name watched a dead 02:31 session all evening on 2026-09-21 while the game wrote to
the 19:54 one; `parseLogFolderName` exists for this. Verified against all nine
folders on the development machine.

Files in a session folder, with the ones we care about marked:

| File | Use |
|---|---|
| `… application_000.log` | **yes** — map load, matching, raid creation |
| `… output_000.log` | **yes** — `LocationLoaded`, `GameStarted`, raid end |
| `… backend_000.log` | no — HTTP traffic to BSG backend |
| `… push-notifications_000.log` | maybe — trader messages, match events |
| `… errors_000.log`, `network-*`, `spatial-audio_000.log`, … | no |

## Line format

Pipe-delimited, five fields:

```
2026-09-20 01:36:09.808|1.1.5.1.47510|Info|application|scene preset path:maps/customs_preset.bundle rcid:bigmap.scenespreset.asset
└─ timestamp ─────────┘ └─ version ─┘ └lvl┘ └channel┘ └─ message ─────────────────────────────────────────────────────────────┘
```

`output_000.log` nests a second channel inside the message field
(`…|Info|output|application|LocationLoaded:…`), so a parser that splits on `|` and
takes field 5 will see `application` rather than the payload for those lines. Split
with a limit, or match on the tail.

Messages are **not guaranteed to be single-line** — the game pretty-prints JSON blocks
across many lines (the settings dump at startup is ~200 lines). A line without a
leading timestamp is a continuation of the previous record.

## Raid lifecycle

Five events, in order. All were observed 8 times in the fixture session.

### 1. Map begins loading — earliest signal, ~30 s of lead time

```
Info|application|scene preset path:maps/customs_preset.bundle rcid:bigmap.scenespreset.asset
```

Gives both the asset name (`customs_preset`) and the internal map id (`bigmap`). This
is the best moment to start prefetching map tiles and quest data.

### 2. Matching progress

```
Debug|application|TRACE-NetworkGameMatching G
Debug|application|TRACE-NetworkGameMatching H
Debug|application|TRACE-NetworkGameMatching I
```

Single-letter stages. Useful only for showing queue time; the letters are not
documented and we should not attach meaning to individual ones.

### 3. Location loaded

```
Info|output|application|LocationLoaded:22.03 real:31.8 diff:9.76
```

Numbers are load timings in seconds, not positions.

### 4. Raid created — authoritative map and mode

```
Debug|application|TRACE-NetworkGameCreate profileStatus: 'Profileid: <PROFILE_ID>, Status: Busy,
RaidMode: Online, Ip: <IP>, Port: 17008, Location: bigmap, Sid: <SID>,
GameMode: deathmatch, shortId: <SHORT_ID>'
```

- `Location` — the map, authoritative
- `RaidMode` — `Online` for PvP; PvE sessions report differently
- `shortId` — the raid code shown in-game, useful for correlating with screenshots
- `GameMode: deathmatch` appears for ordinary raids; it is legacy naming and carries
  no useful meaning

### 5. Raid started — the in-raid clock starts here

```
Info|output|application|GameStarted:95.89(95.89) real:113.1(113.1) diff:17.2
```

### 6. Raid ended

```
EFT.SessionResultShowOperation:Execute(IEftSession, String, Profile, Profile, Location,
ExitStatus, TimeSpan, RaidSettings, Func`1)
```

This appears in `output_000.log` as part of a stack trace, which makes it a weaker
signal than the others — it is a symbol name, not a purpose-built log message, so it
may move between game versions. Treat raid-end detection as best-effort and add a
fallback: if no position ping and no log activity arrive for N minutes, assume the raid
is over.

## Map identifiers — mind the capitalisation

The `Location` field is **not consistently cased**. The same fixture contains:

```
Location: bigmap      (lowercase)
Location: Shoreline   (capitalised)
Location: Sandbox     (capitalised)
```

**Always compare case-insensitively.** This is exactly the kind of thing that works in
testing and then fails on one map in production.

Internal id → tarkov.dev `normalizedName`:

| Internal | tarkov.dev |
|---|---|
| `bigmap` | `customs` |
| `factory4_day`, `factory4_night` | `factory` |
| `woods` | `woods` |
| `shoreline` | `shoreline` |
| `rezervbase` | `reserve` |
| `interchange` | `interchange` |
| `laboratory` | `the-lab` |
| `lighthouse` | `lighthouse` |
| `tarkovstreets` | `streets-of-tarkov` |
| `sandbox`, `sandbox_high` | `ground-zero` |
| `labyrinth` | `the-labyrinth` |
| `terminal` | `terminal` |
| `icebreaker` | `icebreaker` |

Only `bigmap`, `shoreline` and `sandbox` are confirmed from local logs; the rest are
from community sources and each should be confirmed the first time it is seen. Log an
unknown id loudly rather than silently falling back to a default map.

## Watching strategy

- Find the newest `log_*` folder at startup, then watch the parent for new folders —
  a new folder appears each time the game is relaunched.
- Tail with `ReadDirectoryChangesW` on the folder plus a size-delta read; the game
  keeps the file handle open and appends.
- Seek to the end on first open. Replaying an entire session's history on startup would
  announce raids that ended hours ago.
- `output_000.log` reached 59 306 lines in a single session, so never re-read from the
  start on each change notification.

## Quest progress: `push-notifications_000.log`

**Verified** against every log session on this machine (8 sessions, 91 quest events).

Trader messages arrive as chat notifications: a timestamped header line
`…|push-notifications|Got notification | ChatMessageReceived` followed by a JSON body
spread over many lines. Quest state changes are encoded in the message's `type` and
in the **suffix** of `templateId`:

| type | templateId | meaning |
|---|---|---|
| 10 | `<questId> description` | quest started |
| 12 | `<questId> successMessageText` | quest completed (carries reward items) |
| 11 | `<questId> failMessageText` | quest failed |

**The `text` field lies.** Completion messages carry `"text": "quest started"` too —
every one of the 24 completions on this machine does. A parser keyed on `text` would
record every completion as a start. Parse by type and suffix only. The suffix can
also carry extra words (`successMessageText <id> 0`).

The quest ids match tarkov.dev's task ids directly: 63 of 67 started quests resolved;
the rest are presumably event or removed quests and are ignored.

### The game only keeps about five days of logs

Eight session folders here, spanning 2026-09-16 to 2026-09-20. Anything older is gone.
So progress has to be **persisted by us** — `%LOCALAPPDATA%\tarkov-help\progress.json`
— and on first run everything still on disk is imported. Events are merged by their
own `dt` timestamp, so import order cannot roll a quest back.

What the logs can no longer show is recovered two ways: a started quest proves all of
its prerequisites were completed (inferred transitively through tarkov.dev's
`taskRequirements`), and the player can correct anything by hand from the quest panel.

### Scrubbing these for fixtures

These bodies carry more than ids. `UserMatchCreated` includes a `profileToken` (a
32-hex session token), a `sid` and an `ip`; message `_id`s are 26 hex characters, not
24. A scrub that matches exactly 24 hex characters misses all of them — the first
attempt at this fixture did. Quest ids in `templateId` are public game data and are
kept; everything else is replaced.
