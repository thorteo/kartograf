#include <doctest/doctest.h>

#include <cmath>
#include <set>
#include <string>

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

TEST_CASE("zoomed in, the map fills the screen with no empty margin") {
    // The defect this catches: clamping only the centre leaves the viewport
    // half off the edge of the data, so a chunk of the screen is blank
    // backdrop. On screen that reads as a cropped or broken map rather than as
    // the edge of the world -- which is exactly how it was first reported.
    const auto& map = customs();
    const geo::Projection projection(map);
    const auto bounds = projection.imageBounds();
    REQUIRE(bounds.has_value());

    Viewport view = map::fitToBounds(map, 2560, 1440);

    // Zoom in past the fitted scale so the map is larger than the screen, then
    // try to shove the viewport into each corner in turn.
    view.scale *= 3.0;
    const geo::Vec2 corners[] = {
        {bounds->first.x - 1e5, bounds->first.y - 1e5},
        {bounds->second.x + 1e5, bounds->first.y - 1e5},
        {bounds->first.x - 1e5, bounds->second.y + 1e5},
        {bounds->second.x + 1e5, bounds->second.y + 1e5},
    };

    for (const auto& corner : corners) {
        view.centerBase = corner;
        const Viewport clamped = map::clampToBounds(map, view);

        const auto topLeft = map::screenToBase(clamped, geo::Vec2{0.0, 0.0});
        const auto bottomRight = map::screenToBase(
            clamped, geo::Vec2{static_cast<double>(clamped.screenWidth),
                               static_cast<double>(clamped.screenHeight)});

        CAPTURE(corner.x);
        CAPTURE(corner.y);
        // Every screen corner must still be over map data.
        CHECK(topLeft.x >= bounds->first.x - 1e-6);
        CHECK(topLeft.y >= bounds->first.y - 1e-6);
        CHECK(bottomRight.x <= bounds->second.x + 1e-6);
        CHECK(bottomRight.y <= bounds->second.y + 1e-6);
    }
}

TEST_CASE("cover scale fills the screen on both axes") {
    const auto& map = customs();
    const double cover = map::coverScale(map, 2560, 1440);
    const double fit = map::fitToBounds(map, 2560, 1440).scale;

    CHECK(cover > 0.0);
    // Covering always needs at least as much zoom as fitting.
    CHECK(cover >= fit);

    Viewport view = map::fitToBounds(map, 2560, 1440);
    view.scale = cover;
    view = map::clampToBounds(map, view);

    const geo::Projection projection(map);
    const auto bounds = projection.imageBounds();
    REQUIRE(bounds.has_value());

    const auto topLeft = map::screenToBase(view, geo::Vec2{0.0, 0.0});
    const auto bottomRight = map::screenToBase(view, geo::Vec2{2560.0, 1440.0});
    CHECK(topLeft.x >= bounds->first.x - 1e-6);
    CHECK(topLeft.y >= bounds->first.y - 1e-6);
    CHECK(bottomRight.x <= bounds->second.x + 1e-6);
    CHECK(bottomRight.y <= bounds->second.y + 1e-6);
}

TEST_CASE("zoomed out past the map, it is centred rather than pinned") {
    // When the map genuinely cannot fill the screen there is no right edge to
    // stick to, so centring is the only answer that does not look arbitrary.
    const auto& map = customs();
    const geo::Projection projection(map);
    const auto bounds = projection.imageBounds();
    REQUIRE(bounds.has_value());

    Viewport view = map::fitToBounds(map, 2560, 1440);
    view.scale *= 0.4;  // Smaller than the screen on both axes.
    view.centerBase = geo::Vec2{bounds->first.x - 1e5, bounds->second.y + 1e5};

    const Viewport clamped = map::clampToBounds(map, view);
    CHECK(clamped.centerBase.x ==
          doctest::Approx((bounds->first.x + bounds->second.x) / 2.0));
    CHECK(clamped.centerBase.y ==
          doctest::Approx((bounds->first.y + bounds->second.y) / 2.0));
}

TEST_CASE("following a player near the map edge still fills the screen") {
    // The case that produced the report: the fixture position sits near the
    // southern edge of Customs, so centring on it naively put the map's edge
    // inside the screen and left the lower third empty.
    const auto& map = customs();
    const geo::Projection projection(map);
    const auto bounds = projection.imageBounds();
    REQUIRE(bounds.has_value());

    Viewport view = map::fitToBounds(map, 2560, 1440);
    // What the view does when it first gets a position: scale to cover.
    view.scale = std::max(view.scale, map::coverScale(map, 2560, 1440));
    view.centerBase = projection.toImage(geo::Vec3{236.21, 4.43, 165.98});
    view = map::clampToBounds(map, view);

    const auto bottomRight = map::screenToBase(
        view, geo::Vec2{static_cast<double>(view.screenWidth),
                        static_cast<double>(view.screenHeight)});
    CHECK(bottomRight.y <= bounds->second.y + 1e-6);

    const auto topLeft = map::screenToBase(view, geo::Vec2{0.0, 0.0});
    CHECK(topLeft.y >= bounds->first.y - 1e-6);
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

TEST_CASE("floor selection reports ground as -1 and follows elevation") {
    const auto& customsMap = customs();
    REQUIRE_FALSE(customsMap.layers.empty());

    // Well below any building interior: ground.
    CHECK(map::layerIndexOrGround(customsMap, geo::Vec3{200.0, -900.0, 100.0}) == -1);
    // Nowhere near a building, however high: still ground. Elevation alone is
    // never enough.
    CHECK(map::layerIndexOrGround(customsMap, geo::Vec3{1e6, 5.0, 1e6}) == -1);

    // Inside a real extent from the shipped data: an upper floor.
    const geo::LayerRegion* region = nullptr;
    double insideHeight = 0.0;
    std::size_t expectedLayer = 0;
    for (std::size_t i = 0; i < customsMap.layers.size() && region == nullptr; ++i) {
        for (const auto& extent : customsMap.layers[i].extents) {
            if (!extent.regions.empty()) {
                region = &extent.regions.front();
                insideHeight = (extent.heightMin + extent.heightMax) / 2.0;
                expectedLayer = i;
                break;
            }
        }
    }
    REQUIRE(region != nullptr);

    const double midX = (region->corner1.x + region->corner2.x) / 2.0;
    const double midZ = (region->corner1.y + region->corner2.y) / 2.0;
    const int chosen =
        map::layerIndexOrGround(customsMap, geo::Vec3{midX, insideHeight, midZ});
    CHECK(chosen >= 0);
    CHECK(chosen >= static_cast<int>(expectedLayer));
}

TEST_CASE("the raster coverage of the shipped map data is what we expect") {
    // Not every map publishes raster tiles. Three are SVG-only, and two more
    // have SVG-only upper floors. Probing the CDN confirms the tiles genuinely
    // do not exist rather than being missing from maps.json.
    //
    // This test encodes that reality so it cannot drift unnoticed in either
    // direction: a map losing its tiles would break the view, and a map
    // gaining them is our cue to stop apologising for it in the UI.
    static const std::vector<geo::MapDefinition> maps =
        geo::parseMapsJson(test::readFile(test::dataPath("maps.json")));

    const std::set<std::string> expectedWithoutTiles{
        "streets-of-tarkov", "lighthouse", "terminal"};
    // These two draw at ground level but their upper floors are SVG-only.
    const std::set<std::string> expectedWithoutLayerTiles{"interchange", "shoreline"};

    std::set<std::string> actualWithoutTiles;
    std::set<std::string> actualWithoutLayerTiles;
    int drawableLayers = 0;

    for (const auto& definition : maps) {
        const auto* variant = definition.interactiveVariant();
        if (variant == nullptr || variant->projection != "interactive") {
            continue;
        }
        if (variant->tilePath.empty()) {
            actualWithoutTiles.insert(definition.normalizedName);
        }

        int layersWithTiles = 0;
        for (const auto& layer : variant->layers) {
            CAPTURE(definition.normalizedName);
            CAPTURE(layer.name);
            // Whatever else is true, a declared layer must say which heights
            // and regions it covers, or floor selection cannot use it.
            CHECK_FALSE(layer.extents.empty());
            if (!layer.tilePath.empty()) {
                ++layersWithTiles;
                ++drawableLayers;
            }
        }
        // Only meaningful for a map we can draw at all: "has ground tiles but
        // no upper floors" is a different gap from "has no tiles whatsoever".
        if (!variant->tilePath.empty() && !variant->layers.empty() &&
            layersWithTiles == 0) {
            actualWithoutLayerTiles.insert(definition.normalizedName);
        }
    }

    CHECK(actualWithoutTiles == expectedWithoutTiles);
    CHECK(actualWithoutLayerTiles == expectedWithoutLayerTiles);

    // Customs, Reserve, Factory, Ground Zero, Labs and Icebreaker between them
    // carry well over thirty drawable floors.
    CHECK(drawableLayers > 30);

    // The maps we do render must not lose their tiles.
    for (const char* name : {"customs", "shoreline", "woods", "reserve",
                             "interchange", "factory", "ground-zero"}) {
        CAPTURE(name);
        const auto* definition = geo::findMap(maps, name);
        REQUIRE(definition != nullptr);
        const auto* variant = definition->interactiveVariant();
        REQUIRE(variant != nullptr);
        CHECK_FALSE(variant->tilePath.empty());
    }
}

TEST_CASE("a real Dorms position selects the floor the player is on") {
    // Concrete rather than generic: this is a position from the fixture corpus,
    // standing on the second floor of Dorms on Customs. The three variations
    // below hold the horizontal position fixed and move only the elevation,
    // which is the behaviour that makes the feature worth having.
    const auto& map = customs();
    REQUIRE(map.layers.size() >= 2);

    const double dormsX = 236.21;
    const double dormsZ = 165.98;

    CHECK(map::layerIndexOrGround(map, geo::Vec3{dormsX, 4.43, dormsZ}) == 0);
    CHECK(map::layerIndexOrGround(map, geo::Vec3{dormsX, 0.50, dormsZ}) == -1);
    CHECK(map::layerIndexOrGround(map, geo::Vec3{dormsX, 9.00, dormsZ}) == 1);

    CHECK(map.layers[0].name == "2nd Floor");
    CHECK(map.layers[1].name == "3rd Floor");

    // The same elevation out in the open is still ground level. Height alone
    // never means "upstairs".
    CHECK(map::layerIndexOrGround(map, geo::Vec3{-50.0, 4.43, -50.0}) == -1);
}
