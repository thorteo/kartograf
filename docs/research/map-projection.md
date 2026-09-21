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

## Open question, to be settled by test

`maps.json` `bounds` are emitted by a tarkov.dev debug helper that describes them as
*"already rotated"*, yet for Customs the bounds `[[698, -307], [-372, 237]]` line up
with **raw, unrotated** coordinates from our own screenshots (observed X values
13.88 … 655.06, Z values -142.85 … 89.55 all fall inside). With
`coordinateRotation: 180` the rotated X would be negative and fall outside. So either
the bounds are unrotated, or they are consumed at a different stage than assumed.

This does not block implementation, and it has a decisive empirical test:

> Project all 741 real screenshot positions in `tests/fixtures/screenshots/` through the
> pipeline. Every one of them was taken by a player standing somewhere on the map, so
> every one must land inside the map image. Any systematic mirroring, offset or rotation
> error shows up immediately as points outside the bounds or clustered in one corner.

Write that test before trusting the projection. It is cheap, it uses data we already
have, and it validates the single most load-bearing piece of math in the application.
