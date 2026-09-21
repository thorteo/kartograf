// The decisive test for the projection pipeline.
//
// Screenshots record a position but not which map it was taken on. The logs
// record which map each raid was on but not where the player stood. Correlating
// the two by time yields something neither gives alone: several hundred real
// positions, each labelled with its map.
//
// Every one of those positions was recorded by a player standing somewhere on
// that map, so every one must project inside the map. A mirrored axis, a
// dropped rotation or a bad offset all show up here immediately, which is why
// nothing else in the geometry layer should be trusted until this passes.

#include <doctest/doctest.h>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "core/game/LogParser.h"
#include "core/game/ScreenshotName.h"
#include "core/geo/MapConfig.h"
#include "core/data/EmbeddedMaps.h"
#include "core/geo/Projection.h"
#include "fixtures.h"

using namespace tarkov;

namespace {

struct RaidWindow {
    long long startMillis = 0;
    long long endMillis = 0;
    std::string mapId;
};

/// Reconstructs each raid's time window from the fixture session.
std::vector<RaidWindow> raidWindows() {
    const auto lines = test::splitLines(
        test::readFile(test::fixturePath("logs/raid-lifecycle.log")));

    game::LogParser parser;
    std::vector<RaidWindow> windows;
    std::string pendingMap;
    long long pendingCreated = 0;

    for (const auto& line : lines) {
        const auto event = parser.feedLine(line);
        if (!event) {
            continue;
        }
        const long long at = event->timestamp.millisSinceEpoch();

        if (event->type == game::RaidEventType::RaidCreated) {
            pendingMap = event->mapId;
            pendingCreated = at;
            // The previous raid is over by the time the next one is assigned.
            if (!windows.empty() && windows.back().endMillis == 0) {
                windows.back().endMillis = at;
            }
        } else if (event->type == game::RaidEventType::RaidStarted &&
                   !pendingMap.empty()) {
            RaidWindow window;
            window.startMillis = at;
            window.mapId = pendingMap;
            windows.push_back(window);
            pendingMap.clear();
            (void)pendingCreated;
        }
    }
    // Close the final raid with the longest raid duration in the game.
    if (!windows.empty() && windows.back().endMillis == 0) {
        windows.back().endMillis = windows.back().startMillis + 50LL * 60 * 1000;
    }
    return windows;
}

long long screenshotMillis(const game::ScreenshotName& shot) {
    game::LogTimestamp stamp;
    stamp.year = shot.year;
    stamp.month = shot.month;
    stamp.day = shot.day;
    stamp.hour = shot.hour;
    stamp.minute = shot.minute;
    return stamp.millisSinceEpoch();
}

struct LabelledPosition {
    geo::Vec3 world;
    std::string mapId;
    std::string filename;
};

/// Real positions, each attributed to the map its raid was on.
std::vector<LabelledPosition> labelledPositions() {
    const auto windows = raidWindows();
    std::vector<LabelledPosition> positions;

    for (const auto& name : test::screenshotFixtureNames()) {
        const auto shot = game::parseScreenshotName(name);
        if (!shot) {
            continue;
        }
        const long long at = screenshotMillis(*shot);
        for (const auto& window : windows) {
            if (at >= window.startMillis && at <= window.endMillis) {
                positions.push_back({shot->position, window.mapId, name});
                break;
            }
        }
    }
    return positions;
}

const std::vector<geo::MapDefinition>& maps() {
    static const std::vector<geo::MapDefinition> parsed =
        geo::parseMapsJson(test::readFile(test::dataPath("maps.json")));
    return parsed;
}

}  // namespace

TEST_CASE("maps.json parses into usable variants") {
    REQUIRE(maps().size() >= 13);

    const auto* customs = geo::findMap(maps(), "customs");
    REQUIRE(customs != nullptr);

    const auto* variant = customs->interactiveVariant();
    REQUIRE(variant != nullptr);
    CHECK(variant->projection == "interactive");
    CHECK(variant->hasBounds);
    CHECK(variant->coordinateRotation == doctest::Approx(180.0));
    CHECK(variant->transform[0] != doctest::Approx(0.0));
    CHECK_FALSE(variant->tilePath.empty());
    CHECK_FALSE(variant->layers.empty());
}

TEST_CASE("correlating logs with screenshots yields labelled positions") {
    const auto positions = labelledPositions();
    REQUIRE(positions.size() > 200);

    std::map<std::string, int> perMap;
    for (const auto& position : positions) {
        ++perMap[position.mapId];
    }
    // The fixture session covers three distinct maps.
    CHECK(perMap.size() == 3);
    for (const auto& [mapId, count] : perMap) {
        CAPTURE(mapId);
        CHECK(count > 20);
    }
}

TEST_CASE("every real position falls inside its map's declared bounds") {
    const auto positions = labelledPositions();
    REQUIRE_FALSE(positions.empty());

    for (const auto& position : positions) {
        const auto normalized = geo::mapIdToNormalizedName(position.mapId);
        REQUIRE(normalized.has_value());

        const auto* map = geo::findMap(maps(), *normalized);
        REQUIRE(map != nullptr);
        const auto* variant = map->interactiveVariant();
        REQUIRE(variant != nullptr);
        REQUIRE(variant->hasBounds);

        const geo::Projection projection(*variant);

        CAPTURE(position.filename);
        CAPTURE(*normalized);
        CAPTURE(position.world.x);
        CAPTURE(position.world.z);
        CHECK(projection.containsWorld(position.world));
    }
}

TEST_CASE("projected positions land inside the projected map rectangle") {
    const auto positions = labelledPositions();

    for (const auto& position : positions) {
        const auto normalized = geo::mapIdToNormalizedName(position.mapId);
        REQUIRE(normalized.has_value());
        const auto* variant = geo::findMap(maps(), *normalized)->interactiveVariant();
        const geo::Projection projection(*variant);

        const auto bounds = projection.imageBounds();
        REQUIRE(bounds.has_value());
        const auto image = projection.toImage(position.world);

        CAPTURE(position.filename);
        CAPTURE(image.x);
        CAPTURE(image.y);
        CHECK(image.x >= bounds->first.x);
        CHECK(image.x <= bounds->second.x);
        CHECK(image.y >= bounds->first.y);
        CHECK(image.y <= bounds->second.y);
    }
}

TEST_CASE("projection is affine and reversible in character") {
    const auto* variant = geo::findMap(maps(), "customs")->interactiveVariant();
    const geo::Projection projection(*variant);

    // Distinct world points must not collapse onto the same image point.
    const auto a = projection.toImage(geo::Vec3{0.0, 0.0, 0.0});
    const auto b = projection.toImage(geo::Vec3{100.0, 0.0, 0.0});
    const auto c = projection.toImage(geo::Vec3{0.0, 0.0, 100.0});
    CHECK(a.x != doctest::Approx(b.x));
    CHECK(a.y != doctest::Approx(c.y));

    // Elevation must not move a marker horizontally.
    const auto low = projection.toImage(geo::Vec3{50.0, -10.0, 50.0});
    const auto high = projection.toImage(geo::Vec3{50.0, 40.0, 50.0});
    CHECK(low.x == doctest::Approx(high.x));
    CHECK(low.y == doctest::Approx(high.y));

    // Zoom scales linearly.
    const auto z0 = projection.toImage(geo::Vec3{10.0, 0.0, 20.0}, 0);
    const auto z3 = projection.toImage(geo::Vec3{10.0, 0.0, 20.0}, 3);
    CHECK(z3.x == doctest::Approx(z0.x * 8.0));
    CHECK(z3.y == doctest::Approx(z0.y * 8.0));
}

TEST_CASE("floor selection needs both elevation and position") {
    const auto* customs = geo::findMap(maps(), "customs")->interactiveVariant();
    REQUIRE_FALSE(customs->layers.empty());

    // Find a real extent to probe, so the test tracks the shipped data rather
    // than invented numbers.
    const geo::LayerRegion* region = nullptr;
    double insideHeight = 0.0;
    for (const auto& layer : customs->layers) {
        for (const auto& extent : layer.extents) {
            if (!extent.regions.empty()) {
                region = &extent.regions.front();
                insideHeight = (extent.heightMin + extent.heightMax) / 2.0;
                break;
            }
        }
        if (region != nullptr) {
            break;
        }
    }
    REQUIRE(region != nullptr);

    const double midX = (region->corner1.x + region->corner2.x) / 2.0;
    const double midZ = (region->corner1.y + region->corner2.y) / 2.0;

    // Right place, right height: an upper floor is selected.
    CHECK(geo::selectLayer(*customs, geo::Vec3{midX, insideHeight, midZ}).has_value());

    // Right place, ground height: no upper floor.
    CHECK_FALSE(
        geo::selectLayer(*customs, geo::Vec3{midX, -500.0, midZ}).has_value());

    // Right height, nowhere near a building: no upper floor. This is the half
    // of the rule that is easy to forget.
    CHECK_FALSE(geo::selectLayer(*customs, geo::Vec3{99999.0, insideHeight, 99999.0})
                    .has_value());
}

TEST_CASE("the embedded maps.json matches the file on disk") {
    // The binary ships with maps.json compiled in. If the embedding step ever
    // silently produced something different from the pinned file, every map in
    // the overlay would be subtly wrong while the tests kept passing.
    const auto fromFile = geo::parseMapsJson(test::readFile(test::dataPath("maps.json")));
    const auto embedded = geo::parseMapsJson(data::kMapsJson);

    REQUIRE(embedded.size() == fromFile.size());
    REQUIRE_FALSE(embedded.empty());

    for (std::size_t i = 0; i < embedded.size(); ++i) {
        CAPTURE(embedded[i].normalizedName);
        CHECK(embedded[i].normalizedName == fromFile[i].normalizedName);
        CHECK(embedded[i].variants.size() == fromFile[i].variants.size());
    }

    const auto* customs = geo::findMap(embedded, "customs");
    REQUIRE(customs != nullptr);
    const auto* variant = customs->interactiveVariant();
    REQUIRE(variant != nullptr);
    CHECK(variant->coordinateRotation == doctest::Approx(180.0));
    CHECK_FALSE(variant->tilePath.empty());
}
