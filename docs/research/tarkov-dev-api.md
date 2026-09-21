# tarkov.dev API

**Status:** schema verified against
`https://raw.githubusercontent.com/the-hideout/tarkov-api/main/schema-static.mjs`
(fetched 2026-09-20). Live endpoint was returning HTTP 422 at the time of writing, so
queries below are derived from the schema and must be smoke-tested once against the
live API.

Endpoint: `https://api.tarkov.dev/graphql` (POST, `application/json`).
No API key. Be a good citizen: cache aggressively, refresh on a timer measured in
hours, and send a descriptive `User-Agent`.

## Why this API is the right foundation

It publishes **real in-game 3D coordinates** for quest objectives, not just wiki prose.
That is what makes a true position-aware quest overlay possible.

## Types we depend on

### Quest objectives carry positions

```graphql
type TaskZone {
  id: ID!
  map: Map
  position: MapPosition      # { x, y, z } — game world units
  outline: [MapPosition]     # polygon, when the zone is an area
  top: Float
  bottom: Float              # elevation band — feeds floor selection
}
```

`TaskObjective` is an **interface** with 15 implementing types. Only some carry
positions — query with inline fragments:

| Objective type | Positional data |
|---|---|
| `TaskObjectiveBasic` | `zones` |
| `TaskObjectiveItem` | `zones`, plus `foundInRaid`, `count` |
| `TaskObjectiveMark` | `zones`, `markerItem` |
| `TaskObjectiveQuestItem` | `zones`, **`possibleLocations`**, `questItem` |
| `TaskObjectiveShoot` | `zones` (target/location conditions) |
| `TaskObjectiveExtract` | `exitName`, `zoneNames`, `exitStatus` |
| `TaskObjectiveBuildItem`, `…HideoutStation`, `…PlayerLevel`, `…Skill`, `…TraderLevel`, `…TraderStanding`, `…TaskStatus`, `…Experience`, `…UseItem` | none — not map-relevant |

Several also expose `requiredKeys: [[Item]]`, which lets the overlay tell the player
*which key to bring* for an objective — a genuinely useful feature that TarkovQuestie
does not surface well.

### Maps carry everything else worth drawing

```graphql
type Map {
  id: ID!
  name: String!
  normalizedName: String!
  raidDuration: Int
  players: String
  bosses: [BossSpawn]!
  spawns: [MapSpawn]              # zoneName, position, sides, categories
  extracts: [MapExtract]          # name, faction, position, outline, top/bottom, switches
  transits: [MapTransit]
  locks: [Lock]
  switches: [MapSwitch]
  hazards: [MapHazard]
  lootContainers: [LootContainerPosition]
  lootLoose: [LootLoosePosition]
  stationaryWeapons: [StationaryWeaponPosition]
  btrStops: [MapPositionNamed]
}
```

`raidDuration` gives us the raid clock. Combined with the in-game time parsed from
screenshot filenames (see `eft-screenshot-format.md`), the overlay can show time
remaining.

### Tasks

```graphql
type Task {
  id: ID
  name: String!
  normalizedName: String!
  trader: Trader!
  map: Map
  minPlayerLevel: Int
  kappaRequired: Boolean
  lightkeeperRequired: Boolean
  taskRequirements: [TaskStatusRequirement]!
  objectives: [TaskObjective]!
  failConditions: [TaskObjective]!
  wikiLink: String
}
```

`taskRequirements` forms the quest dependency graph, which is what lets us compute
"available to you right now" rather than just "exists on this map".

Note `neededKeys` on `Task` is deprecated — use `requiredKeys` on the objectives.

## Caching

The full task set is several megabytes of JSON. Strategy:

- Fetch once, store normalised in SQLite.
- Refresh on a long timer and on explicit user request, never during a raid.
- Ship with a bundled snapshot so a first run without network still works.
- Key the cache by game version and API response hash so a wipe or patch invalidates it.

`Language` is available on many queries; since the user plays in Russian, request
Russian names and keep English as a fallback for matching against log output.

## To verify on first live run

- [ ] Endpoint responds and the introspected schema matches this document
- [ ] Rate limits — not documented; observe response headers
- [ ] Whether `zones` is populated for the majority of objectives or only a minority
      (this determines how much of the quest overlay actually has map data)
