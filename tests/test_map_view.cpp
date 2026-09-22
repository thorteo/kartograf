#include <doctest/doctest.h>

#include <set>
#include <vector>

#include <imgui_internal.h>

#include "core/data/EmbeddedMaps.h"
#include "core/geo/MapConfig.h"
#include "core/geo/Projection.h"
#include "core/map/TileMath.h"
#include "imgui_harness.h"
#include "ui/MapView.h"
#include "ui/Strings.h"

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
        return [this](int layer, int zoom, int x, int y) -> ui::TileImage {
            requested.insert({layer, zoom, x, y});
            layers.insert(layer);
            return {static_cast<ImTextureID>(1), false};
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

TEST_CASE("the whole map stays in view, as large as the screen allows") {
    test::HeadlessImGui imgui(2560.0f, 1440.0f);
    const auto& map = customsVariant();
    ui::MapViewState state;
    RecordingTiles tiles;

    const geo::Projection projection(map);
    const auto bounds = projection.imageBounds();
    REQUIRE(bounds.has_value());

    // Positions at the western and eastern ends of Customs, and near its
    // southern edge: wherever the player is, no part of the map is cut off.
    for (const auto& snapshot : {snapshotAt(236.21, 4.43, 165.98, 1000),
                                 snapshotAt(-330.0, 0.0, -100.0, 2000),
                                 snapshotAt(600.0, 0.0, -100.0, 3000)}) {
        imgui.frame([&] {
            ui::drawMapView(state, map, snapshot, snapshot.fix.receivedAtMillis, false,
                            tiles.provider());
        });

        const auto topLeft = map::baseToScreen(state.viewport, bounds->first);
        const auto bottomRight = map::baseToScreen(state.viewport, bounds->second);
        CHECK(topLeft.x >= -0.5);
        CHECK(topLeft.y >= -0.5);
        CHECK(bottomRight.x <= state.viewport.screenWidth + 0.5);
        CHECK(bottomRight.y <= state.viewport.screenHeight + 0.5);

        // And it is as large as it can be: it touches both edges of one axis.
        const double width = bottomRight.x - topLeft.x;
        const double height = bottomRight.y - topLeft.y;
        const bool fillsWidth = width >= state.viewport.screenWidth - 1.0;
        const bool fillsHeight = height >= state.viewport.screenHeight - 1.0;
        CHECK((fillsWidth || fillsHeight));
    }
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
    const ui::Lang saved = ui::language();
    ui::setLanguage(ui::Lang::En);
    CHECK(formatFixAge(0.0) == "0s");
    CHECK(formatFixAge(9.4) == "9s");
    CHECK(formatFixAge(59.0) == "59s");
    CHECK(formatFixAge(60.0) == "1m0s");
    CHECK(formatFixAge(200.0) == "3m20s");
    CHECK(formatFixAge(-5.0) == "0s");

    ui::setLanguage(ui::Lang::Ru);
    CHECK(formatFixAge(14.0) == "14 \xd1\x81");                       // "14 с"
    CHECK(formatFixAge(200.0) == "3 \xd0\xbc\xd0\xb8\xd0\xbd 20 \xd1\x81");  // "3 мин 20 с"
    ui::setLanguage(saved);
}

TEST_CASE("overlay layers follow their toggles") {
    test::HeadlessImGui imgui(1600.0f, 900.0f);
    const auto& map = customsVariant();
    RecordingTiles tiles;

    ui::MapOverlays overlays;
    const geo::Vec3 nearPlayer{236.21, 4.43, 165.98};
    overlays.extracts.push_back({nearPlayer, ui::MarkerIcon::ExtractPmc, "exit"});
    overlays.spawns.push_back({nearPlayer, ui::MarkerIcon::SpawnBoss, "boss"});

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

TEST_CASE("a new fix pulses the marker for a few seconds, then drawing stops") {
    test::HeadlessImGui imgui(1600.0f, 900.0f);
    const auto& map = customsVariant();
    ui::MapViewState state;
    RecordingTiles tiles;
    const auto fix = snapshotAt(236.21, 4.43, 165.98, /*receivedAt=*/1000);

    const auto frameAt = [&](std::int64_t now, const game::RaidSnapshot& snapshot) {
        ui::MapViewResult result;
        imgui.frame([&] {
            result = ui::drawMapView(state, map, snapshot, now, false, tiles.provider());
        });
        return result;
    };

    frameAt(1000, fix);
    CHECK(state.pulseFromMillis == 1000);
    // Mid-pulse the view asks to be drawn again, so the ring can grow.
    CHECK(frameAt(2500, fix).wantsRedraw);
    // Afterwards it does not: an idle map must let the render loop sleep.
    CHECK_FALSE(frameAt(5000, fix).wantsRedraw);

    // The same fix again is not news.
    CHECK(state.pulseFromMillis == 1000);
    // A new one is.
    frameAt(9000, snapshotAt(236.21, 4.43, 165.98, /*receivedAt=*/9000));
    CHECK(state.pulseFromMillis == 9000);
    CHECK(frameAt(9500, snapshotAt(236.21, 4.43, 165.98, 9000)).wantsRedraw);
}

TEST_CASE("hovering a quest marker explains it without making the overlay interactive") {
    test::HeadlessImGui imgui(1600.0f, 900.0f);
    const auto& map = customsVariant();
    ui::MapViewState state;
    RecordingTiles tiles;
    const auto snapshot = snapshotAt(236.21, 4.43, 165.98);

    ui::MapOverlays overlays;
    ui::QuestPlace place;
    place.world = {236.21, 4.43, 165.98};
    place.taskName = "Checking";
    place.detail = "Find the bronze pocket watch";
    overlays.quests.push_back(place);

    const auto tooltipShown = [&](ImVec2 mouse) {
        imgui.io().AddMousePosEvent(mouse.x, mouse.y);
        imgui.frame([&] {
            ui::drawMapView(state, map, snapshot, 1000, /*interactive=*/false,
                            tiles.provider(), overlays);
        });
        const ImGuiWindow* tip = ImGui::FindWindowByName("tarkov-help-tip");
        return tip != nullptr && tip->Active;
    };

    // One frame to fit the view to the player; then find the marker on screen.
    CHECK_FALSE(tooltipShown(ImVec2(-FLT_MAX, -FLT_MAX)));
    const auto marker = map::baseToScreen(
        state.viewport, geo::Projection(map).toImage(place.world));
    const ImVec2 at(static_cast<float>(marker.x), static_cast<float>(marker.y));

    // Click-through, with the game's free pointer passed in: the tooltip shows.
    CHECK(tooltipShown(at));
    // Well away from the marker: nothing.
    CHECK_FALSE(tooltipShown(ImVec2(at.x + 200.0f, at.y + 200.0f)));
    // No usable pointer -- the game has it pinned for mouselook: nothing.
    CHECK_FALSE(tooltipShown(ImVec2(-FLT_MAX, -FLT_MAX)));
}

TEST_CASE("marker names stay on screen, beside the icon where there is room") {
    const ui::ScreenRect screen{ImVec2(0.0f, 0.0f), ImVec2(2560.0f, 1440.0f)};
    const ImVec2 box(180.0f, 22.0f);
    const float iconHalf = 15.0f;

    // In open space: to the right of the icon.
    auto label = ui::placeMarkerLabel(ImVec2(1000.0f, 700.0f), iconHalf, box, screen, {});
    REQUIRE(label);
    CHECK(label->min.x > 1000.0f + iconHalf);

    // An exit on the right edge of a fitted map: to the left, whole.
    label = ui::placeMarkerLabel(ImVec2(2550.0f, 700.0f), iconHalf, box, screen, {});
    REQUIRE(label);
    CHECK(label->max.x < 2550.0f - iconHalf);
    CHECK(label->min.x >= 0.0f);

    // On the bottom edge: lifted back inside.
    label = ui::placeMarkerLabel(ImVec2(1000.0f, 1438.0f), iconHalf, box, screen, {});
    REQUIRE(label);
    CHECK(label->max.y <= 1440.0f);

    // Right side taken by another name: moves to the left rather than vanishing.
    const auto first = ui::placeMarkerLabel(ImVec2(1000.0f, 700.0f), iconHalf, box, screen, {});
    REQUIRE(first);
    label = ui::placeMarkerLabel(ImVec2(1000.0f, 705.0f), iconHalf, box, screen, {*first});
    REQUIRE(label);
    CHECK(label->max.x < 1000.0f);

    // Both sides taken: skipped; the tooltip still names it.
    const std::vector<ui::ScreenRect> both{*first, *label};
    CHECK_FALSE(ui::placeMarkerLabel(ImVec2(1000.0f, 702.0f), iconHalf, box, screen, both));
}

TEST_CASE("one quest's objectives at one spot become one marker") {
    const auto place = [](const char* task, data::ObjectiveKind kind, double x, double y,
                          double z, const char* detail, bool optional = false) {
        ui::QuestPlace p;
        p.taskName = task;
        p.kind = kind;
        p.world = {x, y, z};
        p.detail = detail;
        p.optional = optional;
        return p;
    };
    using data::ObjectiveKind;

    // Fishing Gear, as the data has it: locate the boat, then two items to
    // plant in it, all on one point.
    const auto merged = ui::mergeNearbyQuestPlaces({
        place("Fishing Gear", ObjectiveKind::Visit, 10, 0, 10, "Locate the boat"),
        place("Fishing Gear", ObjectiveKind::Plant, 10, 0, 10, "Plant the vest"),
        place("Fishing Gear", ObjectiveKind::Plant, 11, 0, 12, "Plant the multitool"),
    });
    REQUIRE(merged.size() == 1);
    CHECK(merged[0].kind == ObjectiveKind::Plant);  // the thing to do there
    CHECK(merged[0].detail == "Locate the boat\nPlant the vest\nPlant the multitool");

    // Locate-then-mark takes the marker.
    const auto marked = ui::mergeNearbyQuestPlaces({
        place("Q", ObjectiveKind::Visit, 0, 0, 0, "Locate"),
        place("Q", ObjectiveKind::Mark, 3, 0, 4, "Mark", true),
    });
    REQUIRE(marked.size() == 1);
    CHECK(marked[0].kind == ObjectiveKind::Mark);
    CHECK_FALSE(marked[0].optional);  // one required part makes it required

    // Kept apart: another quest, another spot, another floor, item spawns.
    auto spawnA = place("Q", ObjectiveKind::Find, 0, 0, 0, "Pick up");
    spawnA.isQuestItem = true;
    auto spawnB = spawnA;
    CHECK(ui::mergeNearbyQuestPlaces({
                  place("Q", ObjectiveKind::Visit, 0, 0, 0, "a"),
                  place("R", ObjectiveKind::Mark, 0, 0, 0, "b"),
                  place("Q", ObjectiveKind::Mark, 30, 0, 0, "c"),
                  place("Q", ObjectiveKind::Mark, 0, 6, 0, "d"),
                  spawnA,
                  spawnB,
              })
              .size() == 6);
}

TEST_CASE("only tiles still on their way count as loading") {
    test::HeadlessImGui imgui(1600.0f, 900.0f);
    const auto& map = customsVariant();
    const auto snapshot = snapshotAt(236.21, 0.0, 165.98);

    // A tile that failed, or never existed, is not waited for: counting it
    // kept the overlay presenting every frame for as long as the map was open.
    for (const bool loading : {false, true}) {
        CAPTURE(loading);
        ui::MapViewState state;
        state.preferAbstract = false;
        ui::MapViewResult result;
        imgui.frame([&] {
            result = ui::drawMapView(
                state, map, snapshot, 1000, false,
                [loading](int, int, int, int) { return ui::TileImage{0, loading}; });
        });
        CHECK((result.piecesLoading > 0) == loading);
        CHECK(result.tilesDrawn == 0);
    }
}
