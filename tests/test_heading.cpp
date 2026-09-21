#include <doctest/doctest.h>

#include <cmath>
#include <numbers>

#include "core/geo/Heading.h"
#include "core/game/ScreenshotName.h"
#include "fixtures.h"

using tarkov::geo::angleDifference;
using tarkov::geo::headingDegrees;
using tarkov::geo::Quat;

namespace {

/// A yaw-only rotation about the Y axis, the way the game would record a
/// player looking in a given compass direction on level ground.
Quat yawQuat(double degrees) {
    const double half = degrees * std::numbers::pi / 180.0 / 2.0;
    return Quat{0.0, std::sin(half), 0.0, std::cos(half)};
}

}  // namespace

TEST_CASE("identity rotation faces +Z") {
    CHECK(headingDegrees(Quat{0.0, 0.0, 0.0, 1.0}) == doctest::Approx(0.0));
}

TEST_CASE("round-trips a yaw-only rotation") {
    for (double expected : {0.0, 45.0, 90.0, 135.0, 180.0, 270.0, 359.0}) {
        CAPTURE(expected);
        CHECK(angleDifference(expected, headingDegrees(yawQuat(expected))) ==
              doctest::Approx(0.0).epsilon(1e-9));
    }
}

TEST_CASE("always returns a bearing in [0, 360)") {
    for (const auto& name : tarkov::test::screenshotFixtureNames()) {
        const auto parsed = tarkov::game::parseScreenshotName(name);
        REQUIRE(parsed.has_value());
        const double heading = headingDegrees(parsed->orientation);
        CAPTURE(name);
        CAPTURE(heading);
        CHECK(heading >= 0.0);
        CHECK(heading < 360.0);
        CHECK(std::isfinite(heading));
    }
}

TEST_CASE("pitch does not disturb the bearing") {
    // Players look up and down constantly. The compass bearing has to stay put
    // when they do, or the marker's arrow will swing while they aim.
    const double yaw = 123.0;
    const double halfYaw = yaw * std::numbers::pi / 180.0 / 2.0;

    for (double pitchDegrees : {-60.0, -20.0, 20.0, 60.0}) {
        const double halfPitch = pitchDegrees * std::numbers::pi / 180.0 / 2.0;

        // Composition of a yaw about Y then a pitch about the local X axis.
        const double cy = std::cos(halfYaw), sy = std::sin(halfYaw);
        const double cp = std::cos(halfPitch), sp = std::sin(halfPitch);
        const Quat q{cy * sp, sy * cp, -sy * sp, cy * cp};

        CAPTURE(pitchDegrees);
        CHECK(angleDifference(yaw, headingDegrees(q)) ==
              doctest::Approx(0.0).epsilon(1e-6));
    }
}

TEST_CASE("angleDifference takes the short way round") {
    CHECK(angleDifference(350.0, 10.0) == doctest::Approx(20.0));
    CHECK(angleDifference(10.0, 350.0) == doctest::Approx(-20.0));
    CHECK(angleDifference(0.0, 180.0) == doctest::Approx(180.0));
    CHECK(angleDifference(90.0, 90.0) == doctest::Approx(0.0));
}
