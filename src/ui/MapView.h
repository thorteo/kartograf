#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include <imgui.h>

#include "core/game/RaidState.h"
#include "core/geo/MapConfig.h"
#include "core/map/TileMath.h"

namespace tarkov::ui {

/// Supplies the texture for a tile, or 0 when it is not ready yet.
///
/// A callback rather than a texture cache type, so this file stays free of
/// Direct3D and the view can be built and reasoned about on Linux.
using TileTextureProvider = std::function<ImTextureID(int zoom, int x, int y)>;

/// Everything the map view remembers between frames.
struct MapViewState {
    map::Viewport viewport;
    std::string mapName;      ///< The map the viewport was built for.
    bool followPlayer = true; ///< Re-centre on each new position fix.
    bool initialised = false;
    int activeLayer = -1;     ///< -1 is ground level.
    bool layerAuto = true;
};

struct MapViewResult {
    bool wantsRedraw = false;  ///< The user moved something; draw another frame.
    int tilesRequested = 0;
    int tilesDrawn = 0;
};

/// Draws the full-screen map.
///
/// Shown on demand rather than permanently: a fullscreen composited surface is
/// the most expensive thing the overlay can put on screen.
MapViewResult drawMapView(MapViewState& state, const geo::MapVariant& map,
                          const game::RaidSnapshot& snapshot,
                          std::int64_t nowMillis, bool interactive,
                          const TileTextureProvider& tiles);

}  // namespace tarkov::ui
