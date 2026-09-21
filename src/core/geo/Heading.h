#pragma once

#include "core/geo/Vec.h"

namespace tarkov::geo {

/// Compass bearing the player is facing, in degrees.
///
/// 0 is +Z (the direction the map calls north before rotation), increasing
/// clockwise, always in [0, 360).
///
/// Derived from the quaternion's forward vector, flattened onto the horizontal
/// plane. The map's coordinateRotation still has to be added before drawing;
/// this function deliberately knows nothing about any particular map.
///
/// Sign conventions here are easy to get backwards in a way that looks
/// plausible, so this is validated against real screenshot sequences where the
/// player is walking in a straight line -- the heading should agree with the
/// direction of travel.
double headingDegrees(const Quat& q);

/// Shortest signed difference from `from` to `to`, in (-180, 180].
double angleDifference(double from, double to);

}  // namespace tarkov::geo
