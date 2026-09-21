#!/usr/bin/env python3
"""Independent end-to-end check of the coordinate projection.

Projects real screenshot positions through the same pipeline the C++ code uses,
converts each to a tile index, and asks the CDN whether that tile exists.

This is deliberately *not* a unit test: it needs the network, and a unit test
that fails when the CDN is down is a bad unit test. It is the check that does
not depend on maps.json bounds being correct, so it is worth running by hand
whenever the projection changes.

    python3 scripts/verify_projection_tiles.py [map-normalized-name]
"""
import datetime
import json
import math
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAME_ID_TO_NAME = {"bigmap": "customs", "shoreline": "shoreline", "sandbox": "ground-zero"}


def project(variant, x, z):
    t = variant["transform"]
    r = math.radians(variant.get("coordinateRotation", 0))
    cos_r, sin_r = math.cos(r), math.sin(r)
    rx = x * cos_r - z * sin_r
    rz = x * sin_r + z * cos_r
    return t[0] * rx + t[1], -t[2] * rz + t[3]


def raid_windows():
    path = os.path.join(ROOT, "tests/fixtures/logs/raid-lifecycle.log")
    raids, current = [], None
    for line in open(path, encoding="utf-8", errors="replace"):
        stamp = re.match(r"(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})", line)
        if not stamp:
            continue
        at = datetime.datetime.strptime(stamp.group(1), "%Y-%m-%d %H:%M:%S")
        found = re.search(r"Location: ([A-Za-z0-9_]+)", line)
        if found:
            current = {"map": found.group(1).lower(), "created": at}
        if "GameStarted:" in line and current:
            current["start"] = at
            raids.append(current)
            current = None
    windows = []
    for i, raid in enumerate(raids):
        end = raids[i + 1]["created"] if i + 1 < len(raids) else raid["start"] + datetime.timedelta(minutes=50)
        windows.append((raid["start"], end, raid["map"]))
    return windows


def labelled_positions():
    windows = raid_windows()
    path = os.path.join(ROOT, "tests/fixtures/screenshots/filenames.txt")
    out = {}
    for name in open(path):
        m = re.match(
            r"(\d{4}-\d{2}-\d{2})\[(\d{2})-(\d{2})\]_(-?[\d.]+), (-?[\d.]+), (-?[\d.]+)_",
            name.strip(),
        )
        if not m:
            continue
        when = datetime.datetime.strptime(m.group(1), "%Y-%m-%d").replace(
            hour=int(m.group(2)), minute=int(m.group(3))
        )
        for start, end, game_map in windows:
            if start <= when <= end:
                out.setdefault(game_map, []).append((float(m.group(4)), float(m.group(6))))
                break
    return out


def main():
    maps = {m["normalizedName"]: m for m in json.load(open(os.path.join(ROOT, "data/maps.json")))}
    wanted = sys.argv[1] if len(sys.argv) > 1 else None

    total_ok = total_bad = 0
    for game_id, points in labelled_positions().items():
        name = GAME_ID_TO_NAME.get(game_id)
        if not name or (wanted and name != wanted):
            continue
        variant = maps[name]["maps"][0]
        zoom = variant.get("maxZoom", 6)
        tile_size = variant.get("tileSize", 256)
        template = variant.get("tilePath")
        if not template:
            print(f"\n{name}: no tilePath, skipping")
            continue
        print(f"\n{name}: zoom={zoom} tileSize={tile_size} n={len(points)}")

        seen = set()
        for x, z in points:
            px, py = project(variant, x, z)
            tx = int(px * (2**zoom) // tile_size)
            ty = int(py * (2**zoom) // tile_size)
            if (tx, ty) in seen:
                continue
            seen.add((tx, ty))
            url = template.replace("{z}", str(zoom)).replace("{x}", str(tx)).replace("{y}", str(ty))
            code = subprocess.run(
                ["curl", "-s", "-o", "/dev/null", "-w", "%{http_code}", "-I", url],
                capture_output=True, text=True,
            ).stdout
            if code == "200":
                total_ok += 1
            else:
                total_bad += 1
                print(f"  MISSING {zoom}/{tx}/{ty} (HTTP {code}) from world({x:.1f}, {z:.1f})")
        print(f"  distinct tiles: {len(seen)}")

    print(f"\nexist: {total_ok}   missing: {total_bad}")
    return 1 if total_bad else 0


if __name__ == "__main__":
    sys.exit(main())
