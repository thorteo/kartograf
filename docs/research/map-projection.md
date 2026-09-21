# Projecting game coordinates onto map images

**Status:** pipeline transcribed from tarkov.dev source (`src/pages/map/index.jsx`,
`getCRS` / `applyRotation` / `pos`). One open question, noted at the bottom.

We do not have to invent this. tarkov.dev already solved it, and publishes both the
per-map constants and the map tiles.

## Per-map constants

`https://raw.githubusercontent.com/the-hideout/tarkov-dev/main/src/data/maps.json`

Relevant fields per map variant:

```json
{
  "key": "customs",
  "projection": "interactive",
  "tileSize": 256,
  "minZoom": 2, "maxZoom": 6,
  "transform": [0.239, 168.65, 0.239, 136.35],
  "coordinateRotation": 180,
  "bounds": [[698, -307], [-372, 237]],
  "heightRange": [-1000, 1000],
  "tilePath": "https://assets.tarkov.dev/maps/customs_0.16/main/{z}/{x}/{y}.png",
  "svgPath": "https://assets.tarkov.dev/maps/svg/Customs.svg",
  "layers": [ … ]
}
```

`transform` is `[scaleX, offsetX, scaleZ, offsetZ]`.

We should vendor a pinned copy of this file and refresh it deliberately, rather than
fetching at startup — a breaking change upstream would otherwise break the overlay
mid-raid.

## The pipeline

Game position `(gx, gy, gz)` → image coordinates. Note `gy` is elevation and takes no
part in the horizontal projection.

**1. Drop to 2D.** tarkov.dev's `pos()` is `[position.z, position.x]` — z becomes the
vertical axis, x the horizontal.

**2. Rotate by `coordinateRotation` degrees.**

```
θ  = coordinateRotation * π / 180
rx = gx * cos(θ) - gz * sin(θ)
rz = gx * sin(θ) + gz * cos(θ)
```

**3. Scale and offset.** Leaflet's `L.Transformation(a, b, c, d)` computes
`(a*x + b, c*y + d)`. tarkov.dev builds it with **`scaleY = transform[2] * -1`** — the
vertical scale is negated, because image Y grows downward while world Z grows the other
way. Missing that negation produces a map that is correct but mirrored, which is
surprisingly easy to overlook.

```
px = transform[0] * rx + transform[1]
py = -transform[2] * rz + transform[3]
```

**4. Apply zoom.** The result is at zoom scale 1; multiply by `2^zoom` for the tile
pyramid.

## Choosing the floor

Multi-level maps carry a `layers` array. Each layer has `extents`, and each extent has
a `height: [min, max]` range plus `bounds` polygons with names:

```json
{ "name": "2nd Floor", "svgLayer": "Second_Floor",
  "tilePath": ".../2nd/{z}/{x}/{y}.png", "show": false,
  "extents": [ { "height": [2.7, 6.5],
                 "bounds": [ [[243,190],[165,125],"dorms"], … ] } ] }
```

Floor selection is therefore: take the player's `gy`, find the layer whose height range
contains it **and** whose bounds contain the player's horizontal position. Both
conditions matter — being 5 m above sea level only means "second floor" while standing
inside Dorms.

Layer counts, from `maps.json`: Streets 5, Ground Zero 3, Customs 4, Factory 3,
Interchange 2, Labs 2, Reserve 5, Shoreline 3, **Icebreaker 16**. Woods, Lighthouse,
Labyrinth and Terminal are single-layer.

This is the feature that makes the overlay genuinely useful indoors, and it comes
almost free from data we already need.

## Tiles

`tilePath` is a standard `{z}/{x}/{y}` pyramid of 256 px PNGs on
`assets.tarkov.dev`. Zoom range is typically 2–6. Fetch on demand, cache to disk
permanently keyed by URL, and never ship them in the installer.

## Resolved: bounds are unrotated world coordinates

`maps.json` `bounds` are emitted by a tarkov.dev debug helper that describes them as
*"already rotated"*, which conflicted with what our own data showed. Settled
empirically rather than by reading more source.

**Method.** Screenshots record a position but not a map; logs record a map but not a
position. Correlating the two by timestamp produced 258 real positions, each labelled
with the map its raid was on. Two checks were then run against them.

**Check 1 — do raw positions fall inside the declared bounds?**

| Map | n | observed X | bounds X | span | observed Z | bounds Z | span |
|---|---|---|---|---|---|---|---|
| customs | 75 | -328.4 … 632.2 | -372 … 698 | 90% | -199.2 … 177.7 | -307 … 237 | 69% |
| shoreline | 138 | -1014.2 … 364.3 | -1056 … 504 | 88% | -377.7 … 332.3 | -415 … 618 | 69% |
| ground-zero | 45 | 19.9 … 207.6 | -99 … 249 | 54% | 23.6 … 274.6 | -124 … 364 | 51% |

Every position is inside, and on the larger maps the observed extremes come within a
few percent of the declared edges — so this is a tight fit, not a vacuous one. Bounds
are therefore in **unrotated world coordinates**, the same space as raw screenshot
positions. Compare against them directly, before rotation.

**Check 2 — an independent end-to-end test.** Projecting positions through the full
pipeline at `maxZoom`, dividing by `tileSize`, and asking the CDN whether the resulting
tile exists: **110 distinct tiles across the three maps, all 110 present, none
missing.** A sign error, a bad offset or a dropped rotation would put the indices
outside the pyramid and return 404. This check does not consult `bounds` at all, which
is what makes it worth running alongside the first.

Check 1 lives in `tests/test_projection.cpp` and runs on every build. Check 2 is
`scripts/verify_projection_tiles.py`, kept out of the test suite deliberately — it
needs the network, and a unit test that fails when a CDN is down is a bad unit test.
Run it by hand whenever the projection changes.

**Still unverified:** that a marker lands on the *right building* rather than merely
somewhere plausible on the right map. That needs the tile renderer and a visual
comparison, and should be done before trusting the overlay in a raid.
