#include <doctest/doctest.h>

#include "core/game/ScreenshotName.h"
#include "fixtures.h"

using tarkov::game::parseScreenshotName;

TEST_CASE("parses a known screenshot filename exactly") {
    const auto parsed = parseScreenshotName(
        "2026-09-18[01-14]_616.67, 1.05, -103.50_"
        "0.04218, -0.71487, 0.04329, 0.69664_14.65 (0).png");

    REQUIRE(parsed.has_value());
    CHECK(parsed->year == 2026);
    CHECK(parsed->month == 9);
    CHECK(parsed->day == 18);
    CHECK(parsed->hour == 1);
    CHECK(parsed->minute == 14);

    CHECK(parsed->position.x == doctest::Approx(616.67));
    CHECK(parsed->position.y == doctest::Approx(1.05));
    CHECK(parsed->position.z == doctest::Approx(-103.50));

    CHECK(parsed->orientation.x == doctest::Approx(0.04218));
    CHECK(parsed->orientation.y == doctest::Approx(-0.71487));
    CHECK(parsed->orientation.z == doctest::Approx(0.04329));
    CHECK(parsed->orientation.w == doctest::Approx(0.69664));

    CHECK(parsed->gameTimeHours == doctest::Approx(14.65));
    CHECK(parsed->sequence == 0);
}

TEST_CASE("parses every filename in the real corpus") {
    const auto names = tarkov::test::screenshotFixtureNames();
    REQUIRE(names.size() > 200);

    for (const auto& name : names) {
        CAPTURE(name);
        const auto parsed = parseScreenshotName(name);
        REQUIRE(parsed.has_value());

        // Sanity bounds that hold for any real in-raid position.
        CHECK(parsed->gameTimeHours >= 0.0);
        CHECK(parsed->gameTimeHours < 24.0);
        CHECK(parsed->month >= 1);
        CHECK(parsed->month <= 12);
        CHECK(parsed->hour < 24);
        CHECK(parsed->minute < 60);
    }
}

TEST_CASE("rejects names that do not match the grammar") {
    // The parser is strict on purpose: a name it cannot read means the format
    // changed, and that should surface rather than be guessed at.
    const char* rejected[] = {
        "",
        "screenshot.png",
        "2026-09-18[01-14]_616.67, 1.05_0.0, 0.0, 0.0, 1.0_14.65 (0).png",  // 2 coords
        "2026-09-18[01-14]_616.67,1.05,-103.50_0.0, 0.0, 0.0, 1.0_14.65 (0).png",  // no space
        "2026-09-18[01-14]_616.67, 1.05, -103.50_0.0, 0.0, 0.0, 1.0_14.65 (0).jpg",
        "2026-09-18[01-14]_616.67, 1.05, -103.50_0.0, 0.0, 0.0, 1.0_14.65 (0).png.bak",
        "2026-09-18[0114]_616.67, 1.05, -103.50_0.0, 0.0, 0.0, 1.0_14.65 (0).png",
    };
    for (const char* name : rejected) {
        CAPTURE(name);
        CHECK_FALSE(parseScreenshotName(name).has_value());
    }
}

TEST_CASE("reads a multi-digit collision counter") {
    const auto parsed = parseScreenshotName(
        "2026-09-18[01-14]_1.0, 2.0, 3.0_0.0, 0.0, 0.0, 1.0_14.65 (12).png");
    REQUIRE(parsed.has_value());
    CHECK(parsed->sequence == 12);
}

TEST_CASE("negative coordinates survive the decimal separator") {
    // Guards the reason this parser uses std::from_chars rather than strtod:
    // the user's machine is Russian-locale, where a locale-aware parse would
    // treat '.' as a terminator and silently truncate every coordinate.
    const auto parsed = parseScreenshotName(
        "2026-09-18[01-14]_-372.25, -1.75, -307.125_0.0, 0.0, 0.0, 1.0_6.44 (0).png");
    REQUIRE(parsed.has_value());
    CHECK(parsed->position.x == doctest::Approx(-372.25));
    CHECK(parsed->position.y == doctest::Approx(-1.75));
    CHECK(parsed->position.z == doctest::Approx(-307.125));
}
