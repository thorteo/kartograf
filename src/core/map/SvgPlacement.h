#pragma once

#include <optional>
#include <string>
#include <vector>

#include "core/geo/MapConfig.h"
#include "core/geo/Vec.h"
#include "core/map/SvgRenderer.h"
#include "core/map/TileMath.h"

namespace tarkov::map {

/// The rectangle in base-image coordinates that the abstract drawing fills.
///
/// tarkov.dev stretches the SVG over `svgBounds` when a map declares them and
/// over `bounds` otherwise, so the drawing has no coordinate system of its own
/// -- it is simply an image laid over a known world rectangle.
std::optional<std::pair<geo::Vec2, geo::Vec2>> svgImageRect(
    const geo::MapVariant& map);

/// Converts a rectangle of base-image space into the SVG's own coordinates.
///
/// Returns nullopt when the map declares no bounds or the SVG has no size.
std::optional<SvgRect> imageRectToSvg(const geo::MapVariant& map,
                                      const SvgSize& svgSize,
                                      const geo::Vec2& imageMin,
                                      const geo::Vec2& imageMax);

/// The reverse: where a region of the SVG lands in base-image space.
std::optional<std::pair<geo::Vec2, geo::Vec2>> svgRectToImage(
    const geo::MapVariant& map, const SvgSize& svgSize, const SvgRect& region);

/// The SVG layer ids to show for a floor.
///
/// Always includes the map's base layer, then the chosen floor's, because the
/// upper floors only draw the buildings that have one -- without the ground
/// underneath, the rest of the map would vanish indoors.
///
/// `layerIndex` is -1 for ground level, or an index into MapVariant::layers.
std::vector<std::string> svgLayerIdsFor(const geo::MapVariant& map, int layerIndex);

}  // namespace tarkov::map
