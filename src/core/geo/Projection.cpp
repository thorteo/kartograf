#include "core/geo/Projection.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace tarkov::geo {

Projection::Projection(const MapVariant& map) : map_(&map) {
    const double radians = map.coordinateRotation * std::numbers::pi / 180.0;
    cos_ = std::cos(radians);
    sin_ = std::sin(radians);
}

Vec2 Projection::toImage(const Vec3& world) const {
    // Rotate in the (x, z) plane.
    const double rx = world.x * cos_ - world.z * sin_;
    const double rz = world.x * sin_ + world.z * cos_;

    // Scale and offset. The vertical scale is negated because image Y grows
    // downward. Dropping the minus sign yields a map that looks plausible but
    // is mirrored, which is easy to miss.
    return Vec2{
        map_->transform[0] * rx + map_->transform[1],
        -map_->transform[2] * rz + map_->transform[3],
    };
}

Vec2 Projection::toImage(const Vec3& world, int zoom) const {
    const Vec2 base = toImage(world);
    const double scale = std::pow(2.0, zoom);
    return Vec2{base.x * scale, base.y * scale};
}

double Projection::toImageHeading(double worldHeadingDegrees) const {
    double degrees = std::fmod(worldHeadingDegrees + map_->coordinateRotation, 360.0);
    if (degrees < 0.0) {
        degrees += 360.0;
    }
    return degrees;
}

std::optional<std::pair<Vec2, Vec2>> Projection::imageBounds() const {
    if (!map_->hasBounds) {
        return std::nullopt;
    }
    const Vec2 a = toImage(Vec3{map_->bounds[0].x, 0.0, map_->bounds[0].y});
    const Vec2 b = toImage(Vec3{map_->bounds[1].x, 0.0, map_->bounds[1].y});
    return std::make_pair(Vec2{std::min(a.x, b.x), std::min(a.y, b.y)},
                          Vec2{std::max(a.x, b.x), std::max(a.y, b.y)});
}

bool Projection::containsWorld(const Vec3& world) const {
    if (!map_->hasBounds) {
        return true;
    }
    const auto [xLo, xHi] = std::minmax(map_->bounds[0].x, map_->bounds[1].x);
    const auto [zLo, zHi] = std::minmax(map_->bounds[0].y, map_->bounds[1].y);
    return world.x >= xLo && world.x <= xHi && world.z >= zLo && world.z <= zHi;
}

std::optional<std::size_t> selectLayer(const MapVariant& map, const Vec3& world) {
    const Vec2 horizontal{world.x, world.z};

    // Later layers are higher up, so search from the top down and take the
    // first match. A position inside two overlapping bands belongs to the
    // upper one.
    for (std::size_t i = map.layers.size(); i-- > 0;) {
        for (const auto& extent : map.layers[i].extents) {
            if (world.y < extent.heightMin || world.y > extent.heightMax) {
                continue;
            }
            for (const auto& region : extent.regions) {
                if (region.contains(horizontal)) {
                    return i;
                }
            }
        }
    }
    return std::nullopt;
}

}  // namespace tarkov::geo
