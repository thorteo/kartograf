#include "core/map/TileMath.h"

#include <algorithm>
#include <cmath>

#include "core/geo/Projection.h"

namespace tarkov::map {
namespace {

/// Base image units covered by one tile at a given zoom.
double tileSpanBase(const geo::MapVariant& map, int zoom) {
    return static_cast<double>(map.tileSize) / std::pow(2.0, zoom);
}

}  // namespace

geo::Vec2 baseToScreen(const Viewport& viewport, const geo::Vec2& base) {
    return geo::Vec2{
        (base.x - viewport.centerBase.x) * viewport.scale + viewport.screenWidth / 2.0,
        (base.y - viewport.centerBase.y) * viewport.scale + viewport.screenHeight / 2.0,
    };
}

geo::Vec2 screenToBase(const Viewport& viewport, const geo::Vec2& screen) {
    if (viewport.scale == 0.0) {
        return viewport.centerBase;
    }
    return geo::Vec2{
        (screen.x - viewport.screenWidth / 2.0) / viewport.scale + viewport.centerBase.x,
        (screen.y - viewport.screenHeight / 2.0) / viewport.scale + viewport.centerBase.y,
    };
}

int chooseZoom(const geo::MapVariant& map, double scale) {
    if (scale <= 0.0) {
        return map.minZoom;
    }
    // At zoom z one base unit spans 2^z tile pixels, so the level matching the
    // current scale is log2(scale). Floor rather than round: tiles drawn
    // slightly shrunk stay sharp, tiles magnified read as blur.
    const int wanted = static_cast<int>(std::floor(std::log2(scale)));
    return std::clamp(wanted, map.minZoom, map.maxZoom);
}

std::vector<VisibleTile> visibleTiles(const geo::MapVariant& map,
                                      const Viewport& viewport, int zoom) {
    std::vector<VisibleTile> tiles;
    if (viewport.screenWidth <= 0 || viewport.screenHeight <= 0 ||
        viewport.scale <= 0.0) {
        return tiles;
    }

    const double span = tileSpanBase(map, zoom);
    if (span <= 0.0) {
        return tiles;
    }

    const geo::Vec2 topLeftBase = screenToBase(viewport, geo::Vec2{0.0, 0.0});
    const geo::Vec2 bottomRightBase = screenToBase(
        viewport, geo::Vec2{static_cast<double>(viewport.screenWidth),
                            static_cast<double>(viewport.screenHeight)});

    const int firstX = static_cast<int>(std::floor(topLeftBase.x / span));
    const int lastX = static_cast<int>(std::floor(bottomRightBase.x / span));
    const int firstY = static_cast<int>(std::floor(topLeftBase.y / span));
    const int lastY = static_cast<int>(std::floor(bottomRightBase.y / span));

    // A hard ceiling: a bad scale should degrade to drawing less, never to
    // queueing tens of thousands of tile requests.
    constexpr int kMaxTiles = 512;

    for (int y = firstY; y <= lastY; ++y) {
        for (int x = firstX; x <= lastX; ++x) {
            if (x < 0 || y < 0) {
                continue;  // Outside the pyramid.
            }
            if (static_cast<int>(tiles.size()) >= kMaxTiles) {
                return tiles;
            }
            VisibleTile tile;
            tile.zoom = zoom;
            tile.x = x;
            tile.y = y;
            tile.topLeft = baseToScreen(viewport, geo::Vec2{x * span, y * span});
            tile.bottomRight =
                baseToScreen(viewport, geo::Vec2{(x + 1) * span, (y + 1) * span});
            tiles.push_back(tile);
        }
    }
    return tiles;
}

Viewport fitToBounds(const geo::MapVariant& map, int screenWidth, int screenHeight,
                     double padding) {
    Viewport viewport;
    viewport.screenWidth = screenWidth;
    viewport.screenHeight = screenHeight;

    const geo::Projection projection(map);
    const auto bounds = projection.imageBounds();
    if (!bounds || screenWidth <= 0 || screenHeight <= 0) {
        viewport.scale = 1.0;
        return viewport;
    }

    const double width = bounds->second.x - bounds->first.x;
    const double height = bounds->second.y - bounds->first.y;
    viewport.centerBase = geo::Vec2{(bounds->first.x + bounds->second.x) / 2.0,
                                    (bounds->first.y + bounds->second.y) / 2.0};

    if (width <= 0.0 || height <= 0.0) {
        viewport.scale = 1.0;
        return viewport;
    }
    viewport.scale = std::min(screenWidth / width, screenHeight / height) * padding;
    return viewport;
}

Viewport zoomAbout(const Viewport& viewport, const geo::Vec2& anchorScreen,
                   double factor, double minScale, double maxScale) {
    Viewport out = viewport;
    const double target = std::clamp(viewport.scale * factor, minScale, maxScale);
    if (target == viewport.scale) {
        return out;
    }

    // Keep the map point under the anchor fixed: find it before the zoom, then
    // shift the centre so it lands back under the same pixel afterwards.
    const geo::Vec2 anchorBase = screenToBase(viewport, anchorScreen);
    out.scale = target;

    const geo::Vec2 afterBase = screenToBase(out, anchorScreen);
    out.centerBase.x += anchorBase.x - afterBase.x;
    out.centerBase.y += anchorBase.y - afterBase.y;
    return out;
}

Viewport clampToBounds(const geo::MapVariant& map, Viewport viewport) {
    const geo::Projection projection(map);
    const auto bounds = projection.imageBounds();
    if (!bounds) {
        return viewport;
    }
    viewport.centerBase.x =
        std::clamp(viewport.centerBase.x, bounds->first.x, bounds->second.x);
    viewport.centerBase.y =
        std::clamp(viewport.centerBase.y, bounds->first.y, bounds->second.y);
    return viewport;
}

}  // namespace tarkov::map
