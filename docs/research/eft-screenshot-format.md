# Escape from Tarkov screenshot filename format

**Status:** verified against 741 real screenshots on this machine (2026-09-18 … 2026-09-20).

This is the mechanism that gives us the player's position without touching the game
process. When the player presses the in-game screenshot key, the game encodes its own
position and camera orientation into the filename.

Location: `%USERPROFILE%\Documents\Escape from Tarkov\Screenshots\`

## Grammar

```
YYYY-MM-DD[HH-MM]_X, Y, Z_QX, QY, QZ, QW_T (N).png
```

Real example:

```
2026-09-18[01-14]_616.67, 1.05, -103.50_0.04218, -0.71487, 0.04329, 0.69664_14.65 (0).png
```

| Field | Example | Meaning |
|---|---|---|
| `YYYY-MM-DD` | `2026-09-18` | Wall-clock date |
| `[HH-MM]` | `[01-14]` | Wall-clock time, **minute resolution only** |
| `X, Y, Z` | `616.67, 1.05, -103.50` | Player position in Unity world units |
| `QX, QY, QZ, QW` | `0.04218, -0.71487, …` | Camera orientation quaternion |
| `T` | `14.65` | **In-game time of day, in hours** (see below) |
| `(N)` | `(0)` | Collision counter, incremented when a name already exists |

Note the separator is `, ` (comma **and space**) inside a group, and `_` between
groups. A naive split on `,` works; a split on `, ` is safer.

## Axis convention

Unity's left-handed Y-up system:

- `X` — east/west
- `Y` — **elevation**, the value used to pick the floor of a multi-level map
- `Z` — north/south

Map projection consumes `X` and `Z` only; `Y` selects the layer. See
`map-projection.md`.

## The trailing number is in-game time

This was not obvious and is worth recording because it gives us the in-raid clock for
free.

Hypothesis: `T` is the in-game time of day in hours.

Verification: for every consecutive pair of screenshots taken on the same day within a
plausible window, we computed (change in `T`, in minutes) ÷ (change in wall clock, in
minutes) across 35 sample pairs:

```
median ratio  6.80x
p25 / p75     6.60 / 7.11
range of T    6.44 … 22.28
```

Escape from Tarkov is documented to run its day/night cycle at 7× real time. The
measured ratio matches, and the spread is explained by the minute-resolution timestamp
(±1 minute of quantisation error at each end of a 3–25 minute interval). The observed
range of `T` fits hours-of-day. Hypothesis accepted.

Consequence: `14.65` means 14:39 in-game. We can display the in-game clock and, because
raid duration is known per map from the tarkov.dev API, estimate time remaining.

## Heading from the quaternion

We need the compass bearing the player is facing. Take the quaternion's forward vector
and project it onto the horizontal plane:

```
forward.x = 2 * (qx*qz + qw*qy)
forward.z = 1 - 2 * (qx*qx + qy*qy)
yaw_radians = atan2(forward.x, forward.z)
```

Then apply the map's `coordinateRotation` before drawing. Verify the result against a
known landmark before trusting it — sign conventions here are easy to get backwards,
and the fixture set contains sequences where the player is visibly walking in a
straight line, which makes an error obvious.

## Consequences for the design

- **Position is a ping, not a stream.** It updates only when the player presses the
  screenshot key. Between pings we have no information.
- The UI must therefore show the **age** of the fix, and should visibly degrade
  confidence as it gets older rather than implying the marker is live.
- Minute-resolution timestamps make the filename useless for ordering shots taken in
  the same minute; use the `(N)` counter and the file's creation time to disambiguate.
- Screenshots accumulate indefinitely — 741 in three days here. The watcher must handle
  a large existing directory on startup without stalling, and should only parse names,
  never open the image data.
