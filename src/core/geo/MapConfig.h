#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/geo/Vec.h"

namespace tarkov::geo {

/// A named rectangle within a floor, in the same space as MapVariant::bounds.
struct LayerRegion {
    Vec2 corner1;
    Vec2 corner2;
    std::string name;

    bool contains(const Vec2& p) const;
};

/// One elevation band of a floor, with the regions it applies to.
///
/// Both conditions matter: standing 5 m up only means "second floor" while
/// also standing inside Dorms.
struct LayerExtent {
    double heightMin = 0.0;
    double heightMax = 0.0;
    std::vector<LayerRegion> regions;
};

/// One floor of a multi-level map.
struct MapLayer {
    std::string name;
    std::string svgLayer;
    std::string tilePath;
    bool showByDefault = false;
    std::vector<LayerExtent> extents;
};

/// A single renderable view of a map, as published in tarkov.dev's maps.json.
struct MapVariant {
    std::string key;
    std::string projection;  ///< "interactive", "2D" or "3D". We use interactive.

    /// [scaleX, offsetX, scaleZ, offsetZ]. Note Projection negates the vertical
    /// scale; see Projection.h.
    std::array<double, 4> transform{{1.0, 0.0, 1.0, 0.0}};
    double coordinateRotation = 0.0;

    /// Two opposite corners, as [x, z] pairs.
    std::array<Vec2, 2> bounds{};
    bool hasBounds = false;

    int tileSize = 256;
    int minZoom = 0;
    int maxZoom = 0;
    std::string tilePath;
    std::string svgPath;

    std::vector<MapLayer> layers;
};

/// A map, with its renderable variants.
struct MapDefinition {
    std::string normalizedName;
    std::vector<MapVariant> variants;

    /// The variant the overlay draws: the interactive one, else the first.
    const MapVariant* interactiveVariant() const;
};

/// Parses a pinned copy of tarkov.dev's maps.json.
///
/// The file is vendored at data/maps.json rather than fetched at startup, so an
/// upstream change cannot break the overlay in the middle of a raid. Refresh it
/// deliberately. See docs/research/map-projection.md.
std::vector<MapDefinition> parseMapsJson(std::string_view json);

/// Looks up a map by tarkov.dev normalized name. Case-insensitive.
const MapDefinition* findMap(const std::vector<MapDefinition>& maps,
                             std::string_view normalizedName);

/// Translates an internal map id from the game's logs (`bigmap`, `Shoreline`,
/// `sandbox`) to a tarkov.dev normalized name (`customs`, `shoreline`,
/// `ground-zero`).
///
/// The game is not consistent about capitalisation -- the same session writes
/// `bigmap` lowercase and `Shoreline` capitalised -- so matching here is
/// case-insensitive. Returns nullopt for an unrecognised id; callers should log
/// that loudly rather than guessing a default map.
std::optional<std::string_view> mapIdToNormalizedName(std::string_view gameMapId);

}  // namespace tarkov::geo
