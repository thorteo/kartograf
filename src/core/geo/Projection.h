#pragma once

#include <optional>
#include <string>

#include "core/geo/MapConfig.h"
#include "core/geo/Vec.h"

namespace tarkov::geo {

/// Maps a world position onto its map image.
///
/// The pipeline is transcribed from tarkov.dev's own map page so that our
/// markers land where theirs do. See docs/research/map-projection.md.
///
///   1. drop to 2D           (x, z)
///   2. rotate               by coordinateRotation
///   3. scale and offset     with transform, vertical scale NEGATED
///   4. multiply by 2^zoom   for the tile pyramid
class Projection {
public:
    explicit Projection(const MapVariant& map);

    /// World position to image coordinates at zoom scale 1.
    Vec2 toImage(const Vec3& world) const;

    /// World position to image coordinates at a given tile-pyramid zoom.
    Vec2 toImage(const Vec3& world, int zoom) const;

    /// Rotates a heading from headingDegrees() into the map's frame.
    double toImageHeading(double worldHeadingDegrees) const;

    /// The map's bounds pushed through the same pipeline, giving the rectangle
    /// that valid positions should fall inside.
    ///
    /// Returns nullopt when the variant declares no bounds.
    std::optional<std::pair<Vec2, Vec2>> imageBounds() const;

    /// True when the position lies inside the map's declared bounds, compared
    /// in world space.
    ///
    /// Note this deliberately compares raw x/z against bounds rather than
    /// comparing projected values: measurement against 258 real positions shows
    /// maps.json bounds are expressed in unrotated world coordinates, despite
    /// the upstream debug helper describing them as already rotated. See the
    /// research note.
    bool containsWorld(const Vec3& world) const;

private:
    const MapVariant* map_;
    double cos_ = 1.0;
    double sin_ = 0.0;
};

/// Picks the floor to draw, from elevation and horizontal position.
///
/// Returns the index into MapVariant::layers, or nullopt for the ground level.
/// Both the elevation band and the region have to match: being high up only
/// means "upper floor" inside a building that has one.
std::optional<std::size_t> selectLayer(const MapVariant& map, const Vec3& world);

}  // namespace tarkov::geo
