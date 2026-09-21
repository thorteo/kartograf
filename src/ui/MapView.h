#pragma once

#include <cstdint>
#include <string>
#include <functional>
#include <string>

#include <imgui.h>

#include "core/game/RaidState.h"
#include "core/geo/MapConfig.h"
#include "core/map/TileMath.h"

namespace tarkov::ui {

/// Supplies the texture for a tile, or 0 when it is not ready yet.
///
/// `layer` is -1 for ground level and 0 and up for MapVariant::layers.
///
/// A callback rather than a texture cache type, so this file stays free of
/// Direct3D and the view can be built and reasoned about on Linux.
using TileTextureProvider =
    std::function<ImTextureID(int layer, int zoom, int x, int y)>;

/// Everything the map view remembers between frames.
struct MapViewState {
    /// How solid the map is drawn, 0.15 to 1.0.
    ///
    /// This is an overlay: the game has to stay readable through it, and a map
    /// you cannot see past is a screenshot you have to dismiss rather than a
    /// heads-up display. Adjustable at runtime because the right value depends
    /// on the map and on how bright the scene is.
    float opacity = 0.80f;
    map::Viewport viewport;
    std::string mapName;      ///< The map the viewport was built for.
    bool followPlayer = true; ///< Re-centre on each new position fix.
    bool initialised = false;
    /// -1 is ground level; 0 and up index MapVariant::layers.
    int activeLayer = -1;
    /// Follow the player's elevation. Cleared when a floor is picked by hand.
    bool layerAuto = true;
    /// The fix we last re-centred on. Kept here rather than in a function-local
    /// static so two views, or a restarted view, do not share it.
    std::int64_t lastFollowedFix = 0;
};

struct MapViewResult {
    bool wantsRedraw = false;  ///< The user moved something; draw another frame.
    int tilesRequested = 0;
    int tilesDrawn = 0;
};

/// The floor the player is on, as a display name.
///
/// Returns "Ground" for the base level, and for an unrecognised index the
/// index itself rather than an empty string, so a data change shows up instead
/// of silently vanishing.
std::string layerName(const geo::MapVariant& map, int layer);

/// Draws the full-screen map.
///
/// Shown on demand rather than permanently: a fullscreen composited surface is
/// the most expensive thing the overlay can put on screen.
/// Formats the age of a position fix for the map footer, e.g. "12s" or "3m20s".
std::string formatFixAge(double seconds);

MapViewResult drawMapView(MapViewState& state, const geo::MapVariant& map,
                          const game::RaidSnapshot& snapshot,
                          std::int64_t nowMillis, bool interactive,
                          const TileTextureProvider& tiles);

}  // namespace tarkov::ui
