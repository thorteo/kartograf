# tarkov.dev JSON (REST) API

**Status:** verified against the live service on 2026-09-21. This is the source
the overlay uses for quest data.

tarkov.dev publishes two APIs. The GraphQL one at `api.tarkov.dev/graphql` is the
documented one, and it was **down** while this was written — returning HTTP 422
`"GraphQL server unavailable"` to every request, including `{__typename}`, with their
own status page returning 523. The second, mentioned in passing on
`https://tarkov.dev/api/`, is a plain JSON service and was working:

```
GET https://json.tarkov.dev/endpoints        list of endpoints
GET https://json.tarkov.dev/regular/tasks    2.1 MB, 515 tasks
GET https://json.tarkov.dev/regular/tasks_ru 352 KB, 3657 translated strings
GET https://json.tarkov.dev/regular/maps     8.5 MB, maps + extracts + spawns + loot
```

It is the better fit regardless of the outage: a plain GET caches over HTTP, needs no
query to be built, and returns everything in one request for a dataset we want to
download once and keep.

## Game modes

Paths are `/{gameMode}/...` where the mode is `regular`, `pve` or `pvp-season`
(`https://json.tarkov.dev/endpoints` lists them). The site's "Season" switch is
`pvp-season` on this same API: its source (`the-hideout/tarkov-dev`,
`src/modules/api-request.mjs`) fetches `https://json.tarkov.dev/{mode}/...`.

**Choosing the mode from the logs.** `RaidMode: Online` in the raid-create line does
*not* tell a seasonal profile from a regular PvP one: a season player's raids read
`RaidMode: Online, GameMode: deathmatch` like any other. What does differ is the push
notifier host in `push-notifications_000.log`:
`wss://wsn-pvp-season-01.escapefromtarkov.com/...` in every session on this machine
from 2026-09-16 to 09-22, all of them seasonal. The host for a non-seasonal profile has
not been seen yet, so that half is unverified.

**What `pvp-season` holds** (checked 2026-09-22, prod and `json-dev.tarkov.dev` alike):
489 tasks, all of them also in `regular` (515); the 26 `regular` has besides are the
`*-pvp-zone` quests. Of the 489 in common, 13 differ, and only in `startRewards` /
`finishRewards` -- objectives and zones are the same. So for markers the two are
interchangeable.

**Season 1 "KORD BREACH" quests are missing from every set.** A season player had five
active quests the game had started (push type 10, from Prapor ×2, Ref, Therapist,
Skier; ids `6a4f528221e50974ab006f6f`, `6a4f53f6be2cd3a87c0b708d`,
`6a4f83001b7350af050b2e1f`, `6a5e9a5e11ef8ccb6404a18b`, `6a7637fed31fb1191903fc07`)
that appear in no mode's tasks, quest items, achievements or locales. The season is
mentioned only in two locale strings ("Complete Fence's / Lightkeeper's task line in
Season 1 KORD BREACH …") that belong to no task. `/pvp-season/info`, listed as "info on
current season", answers 404. The GraphQL API was still down ("GraphQL server
unavailable"). Until the data lands, such quests cannot be placed on the map.

## Translatable fields carry keys, not text

This is the part that is easy to get wrong. The base document does **not** contain
names or descriptions. It contains translation keys:

```json
"name": "5936d90786f7742b1420ba5b name",
"objectives": [{ "description": "65732ac3c67dcd96adffa3c7", ... }]
```

All 515 names and all 1441 objective descriptions are keys. `?lang=ru` changes
nothing — the response is byte-identical.

The text comes from a second document, a flat dictionary:

```
GET /regular/tasks_ru   ->   {"data": {"5936d90786f7742b1420ba5b name": "Проба пера"}}
```

Note the path is `tasks_ru`, not `tasks/ru` or `tasks?lang=ru`, both of which 404.

**Some values are empty strings.** The live Russian locale contains entries nobody has
translated yet. That is data, not an error, and the overlay falls back to
`normalizedName` — which is always present and readable (`health-care-privacy-part-2`)
— rather than showing a blank row.

## What the task data gives us

Measured over the live 515-task response:

| Objective type | Count | With zones |
|---|---:|---:|
| `giveItem` | 304 | 0 |
| `visit` | 209 | 197 |
| `shoot` | 196 | 22 |
| `findItem` | 138 | 0 |
| `plantItem` | 126 | 122 |
| `findQuestItem` | 110 | 0 |
| `giveQuestItem` | 99 | 0 |
| `extract` | 86 | 0 |
| `mark` | 83 | 83 |
| others | … | 22 |

446 objectives carry zones, 78 carry required keys, and 57 tasks carry `neededKeys`.

Zones are in **the same world coordinates as everything else here**, with an
elevation band:

```json
{ "id": "Sandbox_1_MedicalArea_exploration",
  "map": "65b8d6f5cdde2479cb2a3125",
  "position": { "x": 156.2, "y": 25.52, "z": -83.59 },
  "outline": [ … ], "top": 28.52, "bottom": 22.52, "terrainElevation": 25.52 }
```

`top`/`bottom` were first read here as what places a marker on its floor. **They are
not floors**: they bound the zone's trigger volume. Across the 608 zones that carry
them (task data of 2026-09-22), the band is a median 2.2 m tall, 75% at most 3 m,
90% at most 7 m. Comparing the player's height with it faded markers on and off while
walking outdoors on Customs: in a real raid the player's height moved between 0.27
and 2.43 m over 37 screenshots, and most active zones' bands switched with it. Which
floor a zone is on comes from the map's own layers (`maps.json` extents: a height
range *and* a building region), the same test that picks the floor the map shows.

`findQuestItem` objectives carry `possibleLocations` with several spawn points each;
the game picks one per raid, so all of them have to be shown.

## Map ids

Tasks reference maps by BSG id, not by name, and several ids share one drawable map:

| id | API name | drawn as |
|---|---|---|
| `59fc81d786f774390775787e` | night-factory | factory |
| `65b8d6f5cdde2479cb2a3125` | ground-zero-21 | ground-zero |
| `68236e8153654e8c1200798a` | ground-zero-tutorial | ground-zero |
| `6a294a5b5eb5f9a1700417b7` | the-lab-dark | the-lab |

Filtering by a single id would make night-Factory quests vanish on Factory. The table
is pinned at `data/map-ids.json` and compiled in; an unrecognised id is reported
rather than guessed.

## Caching

Download once, keep on disk, and record when. TarkovQuestie — which turns out to be
installed on this machine and to read the same endpoints — writes a `.updated`
sibling file with an ISO timestamp, which is a sound and simple approach worth
copying.

**Quest data is deliberately not compiled into the binary**, unlike `maps.json`. It
changes every wipe, and a stale quest marker is worse than no marker: it sends the
player across a map to where an objective used to be. Map geometry does not move;
quests do.

## Things worth rechecking

- [ ] Whether the GraphQL API returns and whether its shape differs in any way that
      matters — the REST data is the same underlying dataset, but that is an
      assumption until compared
- [ ] Whether `/regular/maps` is worth fetching for extracts and spawns, at 8.5 MB
- [ ] Rate limits: the docs say there are none, which is worth not abusing
