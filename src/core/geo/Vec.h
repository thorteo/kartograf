#pragma once

namespace tarkov::geo {

/// A position in the game's world, in Unity units.
///
/// Unity is left-handed and Y-up: x is east/west, z is north/south, and y is
/// elevation. Map projection consumes x and z; y selects which floor of a
/// multi-level map the player is on.
struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

/// A point in map-image space, in pixels at zoom scale 1.
struct Vec2 {
    double x = 0.0;
    double y = 0.0;
};

/// Camera orientation as the game records it.
struct Quat {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double w = 1.0;
};

}  // namespace tarkov::geo
