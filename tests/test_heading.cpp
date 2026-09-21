#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <vector>
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

TEST_CASE("facing agrees with the direction of travel in real data") {
    // The check Heading.h asks for: sign conventions here are easy to get
    // backwards in a way that still looks plausible, so validate against
    // players who were actually walking.
    //
    // Pair up screenshots taken close together and moving a sensible distance,
    // then compare the bearing derived from the quaternion against the bearing
    // implied by the displacement. Players mostly walk where they look, so
    // these should agree most of the time.
    //
    // Pairs are selected by IN-GAME time, not by the filename timestamp. The
    // filename has minute resolution, and in a minute a player can be anywhere;
    // measured that way the correlation vanishes into noise and looks like a
    // bug. In-game time carries two decimals of an hour at 7x, which is about
    // five real seconds -- fine enough for consecutive steps.
    struct Sample {
        double gameTime;
        tarkov::geo::Vec3 position;
        tarkov::geo::Quat orientation;
    };

    std::vector<Sample> samples;
    for (const auto& name : tarkov::test::screenshotFixtureNames()) {
        const auto shot = tarkov::game::parseScreenshotName(name);
        if (shot) {
            samples.push_back({shot->gameTimeHours, shot->position, shot->orientation});
        }
    }
    REQUIRE(samples.size() > 100);
    std::sort(samples.begin(), samples.end(),
              [](const Sample& a, const Sample& b) { return a.gameTime < b.gameTime; });

    std::vector<double> errors;
    for (std::size_t i = 0; i + 1 < samples.size(); ++i) {
        const Sample& a = samples[i];
        const Sample& b = samples[i + 1];

        const double gap = b.gameTime - a.gameTime;
        if (gap <= 0.0 || gap > 0.02) {
            continue;  // Not consecutive enough to be one walk.
        }
        const double dx = b.position.x - a.position.x;
        const double dz = b.position.z - a.position.z;
        const double distance = std::hypot(dx, dz);
        if (distance < 3.0 || distance > 60.0) {
            continue;  // Standing still, or teleported between raids.
        }

        double travel = std::atan2(dx, dz) * 180.0 / std::numbers::pi;
        if (travel < 0.0) {
            travel += 360.0;
        }
        errors.push_back(std::abs(angleDifference(travel, headingDegrees(a.orientation))));
    }

    REQUIRE(errors.size() >= 20);
    std::sort(errors.begin(), errors.end());

    const double median = errors[errors.size() / 2];
    const double within45 =
        static_cast<double>(std::count_if(errors.begin(), errors.end(),
                                          [](double e) { return e < 45.0; })) /
        static_cast<double>(errors.size());

    CAPTURE(errors.size());
    CAPTURE(median);
    CAPTURE(within45);

    // Measured on this corpus: median 11 degrees, 84% within 45. The bounds
    // below are loose enough to tolerate a different fixture set but tight
    // enough that an inverted axis or a swapped sin/cos would fail them --
    // either would push the median past 90.
    CHECK(median < 35.0);
    CHECK(within45 > 0.60);
}
