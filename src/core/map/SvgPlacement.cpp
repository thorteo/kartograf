#include "core/map/SvgPlacement.h"

#include <algorithm>

#include "core/geo/Projection.h"

namespace tarkov::map {

std::optional<std::pair<geo::Vec2, geo::Vec2>> svgImageRect(
    const geo::MapVariant& map) {
    const geo::Projection projection(map);

    if (!map.hasSvgBounds) {
        return projection.imageBounds();
    }

    const geo::Vec2 a = projection.toImage(
        geo::Vec3{map.svgBounds[0].x, 0.0, map.svgBounds[0].y});
    const geo::Vec2 b = projection.toImage(
        geo::Vec3{map.svgBounds[1].x, 0.0, map.svgBounds[1].y});
    return std::make_pair(geo::Vec2{std::min(a.x, b.x), std::min(a.y, b.y)},
                          geo::Vec2{std::max(a.x, b.x), std::max(a.y, b.y)});
}

std::optional<SvgRect> imageRectToSvg(const geo::MapVariant& map,
                                      const SvgSize& svgSize,
                                      const geo::Vec2& imageMin,
                                      const geo::Vec2& imageMax) {
    const auto rect = svgImageRect(map);
    if (!rect || svgSize.width <= 0.0 || svgSize.height <= 0.0) {
        return std::nullopt;
    }
    const double spanX = rect->second.x - rect->first.x;
    const double spanY = rect->second.y - rect->first.y;
    if (spanX <= 0.0 || spanY <= 0.0) {
        return std::nullopt;
    }

    SvgRect out;
    out.x = (imageMin.x - rect->first.x) / spanX * svgSize.width;
    out.y = (imageMin.y - rect->first.y) / spanY * svgSize.height;
    out.width = (imageMax.x - imageMin.x) / spanX * svgSize.width;
    out.height = (imageMax.y - imageMin.y) / spanY * svgSize.height;
    return out;
}

std::optional<std::pair<geo::Vec2, geo::Vec2>> svgRectToImage(
    const geo::MapVariant& map, const SvgSize& svgSize, const SvgRect& region) {
    const auto rect = svgImageRect(map);
    if (!rect || svgSize.width <= 0.0 || svgSize.height <= 0.0) {
        return std::nullopt;
    }
    const double spanX = rect->second.x - rect->first.x;
    const double spanY = rect->second.y - rect->first.y;

    const geo::Vec2 min{rect->first.x + region.x / svgSize.width * spanX,
                        rect->first.y + region.y / svgSize.height * spanY};
    const geo::Vec2 max{min.x + region.width / svgSize.width * spanX,
                        min.y + region.height / svgSize.height * spanY};
    return std::make_pair(min, max);
}

std::vector<std::string> svgLayerIdsFor(const geo::MapVariant& map,
                                        int layerIndex) {
    std::vector<std::string> ids;
    if (!map.svgLayer.empty()) {
        ids.push_back(map.svgLayer);
    }
    if (layerIndex >= 0 &&
        static_cast<std::size_t>(layerIndex) < map.layers.size()) {
        const std::string& layer = map.layers[static_cast<std::size_t>(layerIndex)]
                                       .svgLayer;
        // maps.json names floors that some documents do not carry; the renderer
        // ignores an id it cannot find, so passing it through is harmless.
        if (!layer.empty() && layer != "null") {
            ids.push_back(layer);
        }
    }
    return ids;
}

}  // namespace tarkov::map
