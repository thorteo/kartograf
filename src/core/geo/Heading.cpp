#include "core/geo/Heading.h"

#include <cmath>
#include <numbers>

namespace tarkov::geo {

double headingDegrees(const Quat& q) {
    // Forward vector of the rotation, x and z components only.
    const double forwardX = 2.0 * (q.x * q.z + q.w * q.y);
    const double forwardZ = 1.0 - 2.0 * (q.x * q.x + q.y * q.y);

    double degrees = std::atan2(forwardX, forwardZ) * 180.0 / std::numbers::pi;
    if (degrees < 0.0) {
        degrees += 360.0;
    }
    return degrees;
}

double angleDifference(double from, double to) {
    double delta = std::fmod(to - from, 360.0);
    if (delta > 180.0) {
        delta -= 360.0;
    } else if (delta <= -180.0) {
        delta += 360.0;
    }
    return delta;
}

}  // namespace tarkov::geo
