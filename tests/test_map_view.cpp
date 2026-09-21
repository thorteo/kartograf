#include <doctest/doctest.h>

#include <set>
#include <vector>

#include "core/data/EmbeddedMaps.h"
#include "core/geo/MapConfig.h"
#include "core/geo/Projection.h"
#include "imgui_harness.h"
#include "ui/MapView.h"

using namespace tarkov;

namespace {

const geo::MapVariant& customsVariant() {
    static const std::vector<geo::MapDefinition> maps =
        geo::parseMapsJson(data::kMapsJson);
    static const geo::MapVariant& variant =
        *geo::findMap(maps, "customs")->interactiveVariant();
    return variant;
}

game::RaidSnapshot snapshotAt(double x, double y, double z,
                              std::int64_t receivedAt = 1000) {
    game::RaidSnapshot snapshot;
    snapshot.phase = game::RaidPhase::InRaid;
    snapshot.mapId = "bigmap";
    snapshot.mapNormalized = "customs";
    snapshot.fix.valid = true;
    snapshot.fix.world = {x, y, z};
    snapshot.fix.headingDegrees = 90.0;
    snapshot.fix.receivedAtMillis = receivedAt;
    return snapshot;
}

/// Records which tiles the view asked for, and hands back a dummy texture.
struct RecordingTiles {
    std::set<std::tuple<int, int, int, int>> requested;
    std::set<int> layers;

    ui::TileTextureProvider provider() {
        return [this](int layer, int zoom, int x, int y) -> ImTextureID {
            requested.insert({layer, zoom, x, y});
            layers.insert(layer);
            return static_cast<ImTextureID>(1);
        };
    }
};

}  // namespace

TEST_CASE("the view asks for the ground layer and the floor the player is on") {
    test::HeadlessImGui imgui(1600.0f, 900.0f);
    const auto& map = customsVariant();

    ui::MapViewState state;
    RecordingTiles tiles;

    // Second floor of Dorms.
    imgui.frame([&] {
        ui::drawMapView(state, map, snapshotAt(236.21, 4.43, 165.98), 1000, false,
                        tiles.provider());
    });

    CHECK(state.activeLayer == 0);
    CHECK(state.layerAuto);
    // Ground is always drawn underneath, with the active floor over it.
    CHECK(tiles.layers.count(-1) == 1);
    CHECK(tiles.layers.count(0) == 1);
    CHECK(tiles.layers.size() == 2);
}

TEST_CASE("at ground level only the ground layer is requested") {
    test::HeadlessImGui imgui(1600.0f, 900.0f);
    const auto& map = customsVariant();

    ui::MapViewState state;
    RecordingTiles tiles;

    imgui.frame([&] {
        ui::drawMapView(state, map, snapshotAt(236.21, 0.50, 165.98), 1000, false,
                        tiles.provider());
    });

    CHECK(state.activeLayer == -1);
    CHECK(tiles.layers.size() == 1);
    CHECK(tiles.layers.count(-1) == 1);
}

TEST_CASE("walking upstairs changes the floor without being asked") {
    test::HeadlessImGui imgui(1600.0f, 900.0f);
    const auto& map = customsVariant();
    ui::MapViewState state;
    RecordingTiles tiles;

    imgui.frame([&] {
        ui::drawMapView(state, map, snapshotAt(236.21, 0.50, 165.98, 1000), 1000,
                        false, tiles.provider());
    });
    REQUIRE(state.activeLayer == -1);

    imgui.frame([&] {
        ui::drawMapView(state, map, snapshotAt(236.21, 4.43, 165.98, 2000), 2000,
                        false, tiles.provider());
    });
    CHECK(state.activeLayer == 0);

    imgui.frame([&] {
        ui::drawMapView(state, map, snapshotAt(236.21, 9.00, 165.98, 3000), 3000,
                        false, tiles.provider());
    });
    CHECK(state.activeLayer == 1);
}

TEST_CASE("a hand-picked floor is not overridden by the next fix") {
    test::HeadlessImGui imgui(1600.0f, 900.0f);
    const auto& map = customsVariant();
    ui::MapViewState state;
    RecordingTiles tiles;

    state.layerAuto = false;
    state.activeLayer = 2;

    imgui.frame([&] {
        ui::drawMapView(state, map, snapshotAt(236.21, 0.50, 165.98), 1000, false,
                        tiles.provider());
    });

    CHECK(state.activeLayer == 2);
    CHECK_FALSE(state.layerAuto);
}

TEST_CASE("the first fix fills the screen and centres on the player") {
    test::HeadlessImGui imgui(2560.0f, 1440.0f);
    const auto& map = customsVariant();
    ui::MapViewState state;
    RecordingTiles tiles;

    // The position that produced the "map is not fullscreen" report: near the
    // southern edge of Customs.
    const auto snapshot = snapshotAt(236.21, 4.43, 165.98);
    imgui.frame([&] {
        ui::drawMapView(state, map, snapshot, 1000, false, tiles.provider());
    });

    const geo::Projection projection(map);
    const auto bounds = projection.imageBounds();
    REQUIRE(bounds.has_value());

    const auto topLeft = map::screenToBase(state.viewport, geo::Vec2{0.0, 0.0});
    const auto bottomRight = map::screenToBase(
        state.viewport, geo::Vec2{static_cast<double>(state.viewport.screenWidth),
                                  static_cast<double>(state.viewport.screenHeight)});

    CHECK(topLeft.x >= bounds->first.x - 1e-6);
    CHECK(topLeft.y >= bounds->first.y - 1e-6);
    CHECK(bottomRight.x <= bounds->second.x + 1e-6);
    CHECK(bottomRight.y <= bounds->second.y + 1e-6);
}

TEST_CASE("opacity is clamped to a range that keeps the map usable") {
    test::HeadlessImGui imgui(1600.0f, 900.0f);
    const auto& map = customsVariant();
    RecordingTiles tiles;

    // Fully transparent would be indistinguishable from the map being off, and
    // over 1.0 is meaningless.
    ui::MapViewState low;
    low.opacity = -5.0f;
    imgui.frame([&] {
        ui::drawMapView(low, map, snapshotAt(236.21, 4.43, 165.98), 1000, false,
                        tiles.provider());
    });
    CHECK(low.opacity >= 0.15f);

    ui::MapViewState high;
    high.opacity = 9.0f;
    imgui.frame([&] {
        ui::drawMapView(high, map, snapshotAt(236.21, 4.43, 165.98), 1000, false,
                        tiles.provider());
    });
    CHECK(high.opacity <= 1.0f);
}

TEST_CASE("a map with no tiles draws an explanation instead of empty placeholders") {
    static const std::vector<geo::MapDefinition> maps =
        geo::parseMapsJson(data::kMapsJson);
    const auto* streets = geo::findMap(maps, "streets-of-tarkov");
    REQUIRE(streets != nullptr);
    const auto* variant = streets->interactiveVariant();
    REQUIRE(variant != nullptr);
    REQUIRE(variant->tilePath.empty());

    test::HeadlessImGui imgui(1600.0f, 900.0f);
    ui::MapViewState state;
    RecordingTiles tiles;

    game::RaidSnapshot snapshot;
    snapshot.mapNormalized = "streets-of-tarkov";

    imgui.frame([&] {
        ui::drawMapView(state, *variant, snapshot, 1000, false, tiles.provider());
    });

    // No tile was asked for, so nothing sits in the queue waiting forever.
    CHECK(tiles.requested.empty());
    CHECK(tiles.layers.empty());
    // The viewport is still set up: position tracking keeps working on these
    // maps even though there is no picture to put under it.
    CHECK(state.viewport.screenWidth == 1600);
    CHECK(state.viewport.screenHeight == 900);
}

TEST_CASE("the fix age reads at a glance") {
    using ui::formatFixAge;
    CHECK(formatFixAge(0.0) == "0s");
    CHECK(formatFixAge(9.4) == "9s");
    CHECK(formatFixAge(59.0) == "59s");
    CHECK(formatFixAge(60.0) == "1m0s");
    CHECK(formatFixAge(200.0) == "3m20s");
    CHECK(formatFixAge(-5.0) == "0s");
}

TEST_CASE("overlay layers follow their toggles") {
    test::HeadlessImGui imgui(1600.0f, 900.0f);
    const auto& map = customsVariant();
    RecordingTiles tiles;

    ui::MapOverlays overlays;
    const geo::Vec3 nearPlayer{236.21, 4.43, 165.98};
    overlays.extracts.push_back({nearPlayer, ui::MarkerIcon::ExtractPmc, "exit", {}, {}});
    overlays.spawns.push_back({nearPlayer, ui::MarkerIcon::SpawnBoss, "boss", {}, {}});

    std::set<ui::MarkerIcon> asked;
    overlays.icons = [&asked](ui::MarkerIcon icon) -> ImTextureID {
        asked.insert(icon);
        return 0;  // Not downloaded: exercises the vector fallback.
    };

    ui::MapViewState state;  // spawns default to off
    imgui.frame([&] {
        ui::drawMapView(state, map, snapshotAt(236.21, 4.43, 165.98), 1000, false,
                        tiles.provider(), overlays);
    });
    CHECK(asked.count(ui::MarkerIcon::ExtractPmc) == 1);
    CHECK(asked.count(ui::MarkerIcon::SpawnBoss) == 0);
    CHECK(asked.count(ui::MarkerIcon::Player) == 1);

    asked.clear();
    state.showSpawns = true;
    state.showExtracts = false;
    imgui.frame([&] {
        ui::drawMapView(state, map, snapshotAt(236.21, 4.43, 165.98), 2000, false,
                        tiles.provider(), overlays);
    });
    CHECK(asked.count(ui::MarkerIcon::SpawnBoss) == 1);
    CHECK(asked.count(ui::MarkerIcon::ExtractPmc) == 0);
}
