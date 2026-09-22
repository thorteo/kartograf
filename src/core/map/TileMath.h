#pragma once

#include <vector>

#include "core/geo/MapConfig.h"
#include "core/geo/Vec.h"

namespace tarkov::map {

/// What part of a map is on screen.
///
/// Positions are kept in *base image coordinates* -- the output of
/// geo::Projection at zoom scale 1 -- rather than in pixels at some particular
/// zoom. That keeps panning and zooming independent: changing the zoom never
/// moves what is under the cursor, because the anchor is expressed in a space
/// that does not depend on zoom.
struct Viewport {
    geo::Vec2 centerBase;  ///< Base image coordinate at the centre of the screen.
    double scale = 1.0;    ///< Screen pixels per base image unit.
    int screenWidth = 0;
    int screenHeight = 0;
};

/// A tile to draw, with the screen rectangle it occupies.
struct VisibleTile {
    int zoom = 0;
    int x = 0;
    int y = 0;
    /// Screen-space rectangle, top-left and bottom-right, in pixels.
    geo::Vec2 topLeft;
    geo::Vec2 bottomRight;
};

/// Base image coordinate to screen pixel.
geo::Vec2 baseToScreen(const Viewport& viewport, const geo::Vec2& base);

/// Screen pixel back to base image coordinate.
geo::Vec2 screenToBase(const Viewport& viewport, const geo::Vec2& screen);

/// The tile-pyramid level whose native resolution best matches the current
/// scale, clamped to what the map publishes.
///
/// Picking the level below the exact match keeps tiles from being magnified,
/// which reads as blur; the cost is drawing a few more of them.
int chooseZoom(const geo::MapVariant& map, double scale);

/// Every tile touching the viewport at the given zoom, with screen rectangles.
///
/// Only tiles that exist: the pyramid is 2^zoom tiles square at every zoom,
/// on every map and floor (probed against the CDN on 2026-09-22 -- the corner
/// tile answers 200, the one past it 404). Where a fitted map leaves margins,
/// the screen reaches past the pyramid; those positions are not tiles.
///
/// Tiles are emitted in row-major order so drawing them in sequence produces a
/// predictable overdraw pattern.
std::vector<VisibleTile> visibleTiles(const geo::MapVariant& map,
                                      const Viewport& viewport, int zoom);

/// A viewport that fits the whole map into the given screen size.
///
/// Letterboxes when the aspect ratios differ, which is right for an overview:
/// seeing all of the map matters more than filling the screen.
Viewport fitToBounds(const geo::MapVariant& map, int screenWidth, int screenHeight,
                     double padding = 0.92);

/// The smallest scale at which the map covers the screen completely.
///
/// The counterpart to fitToBounds: centred on the player mid-raid, a screen
/// with empty margins reads as a broken map, and seeing the whole location
/// matters less than seeing your surroundings. Returns 0 when the map declares
/// no bounds.
double coverScale(const geo::MapVariant& map, int screenWidth, int screenHeight);

/// Zooms by `factor` while keeping `anchorScreen` over the same map location.
///
/// This is what makes wheel-zoom feel right: the point under the cursor stays
/// put instead of the map sliding out from under it.
Viewport zoomAbout(const Viewport& viewport, const geo::Vec2& anchorScreen,
                   double factor, double minScale, double maxScale);

/// Keeps the map filling the screen wherever it can.
///
/// Clamping only the centre is not enough: with the centre merely inside the
/// bounds, half the screen can still be empty backdrop, which reads as a
/// cropped or broken map rather than as the edge of the data.
///
/// So along each axis: if the map is wider than the screen at this scale, the
/// centre is constrained so the viewport stays inside the map. If it is
/// narrower, the map is centred instead, since it cannot fill the screen and
/// pinning it to one side would look arbitrary.
Viewport clampToBounds(const geo::MapVariant& map, Viewport viewport);

/// The floor to draw for a world position: geo::selectLayer, with the ground
/// level expressed as -1 rather than an empty optional.
int layerIndexOrGround(const geo::MapVariant& map, const geo::Vec3& world);

}  // namespace tarkov::map
