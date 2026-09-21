#include <doctest/doctest.h>

#include <cmath>

#include "core/geo/Projection.h"
#include "core/map/TileMath.h"
#include "core/game/ScreenshotName.h"
#include "fixtures.h"

using namespace tarkov;
using map::Viewport;

namespace {

const geo::MapVariant& customs() {
    static const std::vector<geo::MapDefinition> maps =
        geo::parseMapsJson(test::readFile(test::dataPath("maps.json")));
    static const geo::MapVariant& variant =
        *geo::findMap(maps, "customs")->interactiveVariant();
    return variant;
}

Viewport viewport(double scale) {
    Viewport out;
    out.centerBase = geo::Vec2{1000.0, 800.0};
    out.scale = scale;
    out.screenWidth = 1600;
    out.screenHeight = 900;
    return out;
}

}  // namespace

TEST_CASE("screen and base coordinates round-trip") {
    const Viewport view = viewport(2.5);
    for (const auto& screen : {geo::Vec2{0.0, 0.0}, geo::Vec2{800.0, 450.0},
                               geo::Vec2{1600.0, 900.0}, geo::Vec2{123.0, 456.0}}) {
        const auto base = map::screenToBase(view, screen);
        const auto back = map::baseToScreen(view, base);
        CHECK(back.x == doctest::Approx(screen.x));
        CHECK(back.y == doctest::Approx(screen.y));
    }
}

TEST_CASE("the viewport centre lands in the middle of the screen") {
    const Viewport view = viewport(3.0);
    const auto screen = map::baseToScreen(view, view.centerBase);
    CHECK(screen.x == doctest::Approx(view.screenWidth / 2.0));
    CHECK(screen.y == doctest::Approx(view.screenHeight / 2.0));
}

TEST_CASE("zoom level tracks scale and stays within what the map publishes") {
    const auto& map = customs();
    CHECK(map::chooseZoom(map, 1.0) == map.minZoom);       // below the range
    CHECK(map::chooseZoom(map, 1e9) == map.maxZoom);       // above it
    CHECK(map::chooseZoom(map, 0.0) == map.minZoom);       // degenerate
    CHECK(map::chooseZoom(map, -5.0) == map.minZoom);

    for (int zoom = map.minZoom; zoom <= map.maxZoom; ++zoom) {
        CAPTURE(zoom);
        CHECK(map::chooseZoom(map, std::pow(2.0, zoom)) == zoom);
        // Just short of the next level must not promote early.
        CHECK(map::chooseZoom(map, std::pow(2.0, zoom + 1) * 0.999) == zoom);
    }
}

TEST_CASE("visible tiles cover the screen without gaps") {
    const auto& map = customs();
    const Viewport view = viewport(8.0);
    const int zoom = map::chooseZoom(map, view.scale);

    const auto tiles = map::visibleTiles(map, view, zoom);
    REQUIRE_FALSE(tiles.empty());

    // Every tile is the same size on screen, and adjacent tiles abut exactly.
    const double width = tiles.front().bottomRight.x - tiles.front().topLeft.x;
    CHECK(width > 0.0);
    for (const auto& tile : tiles) {
        CHECK(tile.bottomRight.x - tile.topLeft.x == doctest::Approx(width));
        CHECK(tile.bottomRight.y - tile.topLeft.y == doctest::Approx(width));
    }

    // Sample the screen: every sampled point must be inside some tile.
    for (int sy = 0; sy <= view.screenHeight; sy += 97) {
        for (int sx = 0; sx <= view.screenWidth; sx += 97) {
            bool covered = false;
            for (const auto& tile : tiles) {
                if (sx >= tile.topLeft.x - 0.001 && sx <= tile.bottomRight.x + 0.001 &&
                    sy >= tile.topLeft.y - 0.001 && sy <= tile.bottomRight.y + 0.001) {
                    covered = true;
                    break;
                }
            }
            CAPTURE(sx);
            CAPTURE(sy);
            CHECK(covered);
        }
    }
}

TEST_CASE("tiles outside the pyramid are not requested") {
    const auto& map = customs();
    Viewport view = viewport(4.0);
    view.centerBase = geo::Vec2{-10000.0, -10000.0};

    for (const auto& tile : map::visibleTiles(map, view, map.minZoom)) {
        CHECK(tile.x >= 0);
        CHECK(tile.y >= 0);
    }
}

TEST_CASE("a degenerate viewport asks for nothing") {
    const auto& map = customs();
    Viewport view = viewport(4.0);

    view.screenWidth = 0;
    CHECK(map::visibleTiles(map, view, 4).empty());

    view = viewport(0.0);
    CHECK(map::visibleTiles(map, view, 4).empty());

    // An absurd scale must degrade to fewer tiles, not to a flood of requests.
    view = viewport(1e6);
    CHECK(map::visibleTiles(map, view, 6).size() <= 512);
}

TEST_CASE("fitting to bounds puts the whole map on screen") {
    const auto& map = customs();
    const Viewport view = map::fitToBounds(map, 1600, 900);

    const geo::Projection projection(map);
    const auto bounds = projection.imageBounds();
    REQUIRE(bounds.has_value());

    const auto topLeft = map::baseToScreen(view, bounds->first);
    const auto bottomRight = map::baseToScreen(view, bounds->second);

    CHECK(topLeft.x >= -1.0);
    CHECK(topLeft.y >= -1.0);
    CHECK(bottomRight.x <= view.screenWidth + 1.0);
    CHECK(bottomRight.y <= view.screenHeight + 1.0);
}

TEST_CASE("zooming keeps the point under the cursor fixed") {
    // The property that makes wheel-zoom feel right. Without it the map slides
    // out from under the pointer on every notch.
    const auto& map = customs();
    Viewport view = map::fitToBounds(map, 1600, 900);

    const geo::Vec2 anchor{420.0, 310.0};
    const geo::Vec2 before = map::screenToBase(view, anchor);

    for (double factor : {1.25, 1.25, 1.25, 0.8, 0.8}) {
        view = map::zoomAbout(view, anchor, factor, 0.01, 1000.0);
        const geo::Vec2 after = map::screenToBase(view, anchor);
        CHECK(after.x == doctest::Approx(before.x).epsilon(1e-9));
        CHECK(after.y == doctest::Approx(before.y).epsilon(1e-9));
    }
}

TEST_CASE("zoom respects its limits") {
    const auto& map = customs();
    Viewport view = map::fitToBounds(map, 1600, 900);
    const geo::Vec2 anchor{800.0, 450.0};

    for (int i = 0; i < 100; ++i) {
        view = map::zoomAbout(view, anchor, 2.0, 0.05, 40.0);
    }
    CHECK(view.scale == doctest::Approx(40.0));

    for (int i = 0; i < 100; ++i) {
        view = map::zoomAbout(view, anchor, 0.5, 0.05, 40.0);
    }
    CHECK(view.scale == doctest::Approx(0.05));
}

TEST_CASE("the map cannot be panned away and lost") {
    const auto& map = customs();
    Viewport view = map::fitToBounds(map, 1600, 900);
    view.centerBase = geo::Vec2{1e6, -1e6};

    view = map::clampToBounds(map, view);

    const geo::Projection projection(map);
    const auto bounds = projection.imageBounds();
    REQUIRE(bounds.has_value());
    CHECK(view.centerBase.x >= bounds->first.x);
    CHECK(view.centerBase.x <= bounds->second.x);
    CHECK(view.centerBase.y >= bounds->first.y);
    CHECK(view.centerBase.y <= bounds->second.y);
}

TEST_CASE("real positions land on screen when the map is fitted") {
    // Ties the viewport to the data: fit Customs to a screen, project the real
    // fixture positions, and they must all be visible.
    static const std::vector<geo::MapDefinition> maps =
        geo::parseMapsJson(test::readFile(test::dataPath("maps.json")));
    const auto& map = *geo::findMap(maps, "customs")->interactiveVariant();

    const Viewport view = map::fitToBounds(map, 1600, 900);
    const geo::Projection projection(map);

    int checked = 0;
    for (const auto& name : test::screenshotFixtureNames()) {
        const auto shot = game::parseScreenshotName(name);
        if (!shot) {
            continue;
        }
        if (!projection.containsWorld(shot->position)) {
            continue;  // From a different map in the same session.
        }
        const auto screen = map::baseToScreen(view, projection.toImage(shot->position));
        CAPTURE(name);
        CHECK(screen.x >= 0.0);
        CHECK(screen.x <= view.screenWidth);
        CHECK(screen.y >= 0.0);
        CHECK(screen.y <= view.screenHeight);
        ++checked;
    }
    CHECK(checked > 50);
}
