#include <doctest/doctest.h>

#include <atomic>
#include <ostream>
#include <thread>
#include <vector>

#include "core/game/RaidState.h"
#include "fixtures.h"

// Found by ADL so doctest can print a RaidPhase in a failure message. Kept in
// the test rather than the header, which would otherwise have to pull in
// <ostream> for the benefit of tests alone.
namespace tarkov::game {
inline std::ostream& operator<<(std::ostream& out, RaidPhase phase) {
    return out << raidPhaseName(phase);
}
}  // namespace tarkov::game

using namespace tarkov;
using game::RaidEvent;
using game::RaidEventType;
using game::RaidPhase;
using game::RaidState;

namespace {

RaidEvent event(RaidEventType type, std::string mapId = {}) {
    RaidEvent out;
    out.type = type;
    out.mapId = std::move(mapId);
    return out;
}

game::ScreenshotName shotAt(double x, double y, double z) {
    game::ScreenshotName shot;
    shot.position = {x, y, z};
    shot.orientation = {0.0, 0.0, 0.0, 1.0};
    shot.gameTimeHours = 14.65;
    return shot;
}

}  // namespace

TEST_CASE("walks the raid lifecycle") {
    RaidState state;
    CHECK(state.snapshot()->phase == RaidPhase::Idle);

    state.apply(event(RaidEventType::MapLoading, "bigmap"), 1000);
    {
        const auto snap = state.snapshot();
        CHECK(snap->phase == RaidPhase::Loading);
        CHECK(snap->mapId == "bigmap");
        CHECK(snap->mapNormalized == "customs");
    }

    state.apply(event(RaidEventType::MatchingStage), 2000);
    CHECK(state.snapshot()->phase == RaidPhase::Matching);

    state.apply(event(RaidEventType::LocationLoaded), 3000);
    CHECK(state.snapshot()->phase == RaidPhase::Loaded);

    state.apply(event(RaidEventType::RaidStarted), 4000);
    {
        const auto snap = state.snapshot();
        CHECK(snap->phase == RaidPhase::InRaid);
        CHECK(snap->raidElapsedSeconds(64000) == doctest::Approx(60.0));
    }

    state.apply(event(RaidEventType::RaidEnded), 5000);
    CHECK(state.snapshot()->phase == RaidPhase::Ended);
}

TEST_CASE("the server assignment overrides the scene preset's map") {
    RaidState state;
    state.apply(event(RaidEventType::MapLoading, "bigmap"), 1000);

    RaidEvent created = event(RaidEventType::RaidCreated, "Shoreline");
    created.raidMode = "Online";
    created.shortId = "ABC123";
    state.apply(created, 2000);

    const auto snap = state.snapshot();
    CHECK(snap->mapId == "Shoreline");
    CHECK(snap->mapNormalized == "shoreline");
    CHECK(snap->raidMode == "Online");
    CHECK(snap->shortId == "ABC123");
}

TEST_CASE("an offline raid's location sets the map and leaves the raid mode alone") {
    RaidState state;
    RaidEvent created = event(RaidEventType::RaidCreated, "bigmap");
    created.raidMode = "Online";
    state.apply(created, 500);

    // Training, later in the session.
    state.apply(event(RaidEventType::MapLoading, "Shopping_Mall"), 1000);
    state.apply(event(RaidEventType::LocationNamed, "Interchange"), 2000);

    const auto snap = state.snapshot();
    CHECK(snap->mapId == "Interchange");
    CHECK(snap->mapNormalized == "interchange");
    CHECK(snap->raidMode.empty());  // cleared by the new raid, not set offline
}

TEST_CASE("an unrecognised map leaves the normalized name empty") {
    // Better an empty name the UI can report than a silent fallback to the
    // wrong map.
    RaidState state;
    state.apply(event(RaidEventType::MapLoading, "brand_new_map"), 1000);

    const auto snap = state.snapshot();
    CHECK(snap->mapId == "brand_new_map");
    CHECK(snap->mapNormalized.empty());
}

TEST_CASE("a new raid clears the previous one") {
    RaidState state;
    state.apply(event(RaidEventType::MapLoading, "bigmap"), 1000);
    state.apply(event(RaidEventType::RaidStarted), 2000);
    state.applyPosition(shotAt(1.0, 2.0, 3.0), 3000);
    REQUIRE(state.snapshot()->fix.valid);

    state.apply(event(RaidEventType::MapLoading, "Shoreline"), 9000);
    const auto snap = state.snapshot();
    CHECK(snap->mapNormalized == "shoreline");
    CHECK_FALSE(snap->fix.valid);
    CHECK(snap->fixCount == 0);
    CHECK(snap->raidStartedAtMillis == 0);
}

TEST_CASE("a position fix records its own age") {
    RaidState state;
    state.applyPosition(shotAt(10.0, 1.0, -20.0), 5000);

    const auto snap = state.snapshot();
    REQUIRE(snap->fix.valid);
    CHECK(snap->fix.world.x == doctest::Approx(10.0));
    CHECK(snap->fix.world.z == doctest::Approx(-20.0));
    CHECK(snap->fix.ageSeconds(5000) == doctest::Approx(0.0));
    CHECK(snap->fix.ageSeconds(35000) == doctest::Approx(30.0));
    // A clock that moves backwards must not produce a negative age.
    CHECK(snap->fix.ageSeconds(1000) == doctest::Approx(0.0));
}

TEST_CASE("fixes arriving before the log catches up are kept") {
    // The game writes the screenshot immediately; our log tail may lag. The
    // first fix of a raid is the one most worth having.
    RaidState state;
    state.applyPosition(shotAt(1.0, 1.0, 1.0), 1000);
    CHECK(state.snapshot()->fix.valid);

    state.apply(event(RaidEventType::RaidStarted), 2000);
    CHECK(state.snapshot()->fix.valid);
}

TEST_CASE("an idle snapshot is handed out without reallocating") {
    // The renderer takes a snapshot every frame. If that allocated each time,
    // an idle overlay would churn the heap for nothing.
    RaidState state;
    const auto first = state.snapshot();
    const auto second = state.snapshot();
    CHECK(first.get() == second.get());

    state.applyPosition(shotAt(1.0, 1.0, 1.0), 1000);
    const auto third = state.snapshot();
    CHECK(third.get() != first.get());
}

TEST_CASE("a snapshot stays coherent while the writer keeps going") {
    // The guarantee ADR 0004 exists to provide: a reader never sees a torn mix
    // of two states. Run under ThreadSanitizer to make this meaningful.
    RaidState state;
    std::atomic<bool> stop{false};

    std::thread writer([&] {
        for (int i = 0; !stop.load(std::memory_order_relaxed); ++i) {
            state.apply(event(RaidEventType::MapLoading, "bigmap"), i);
            state.applyPosition(shotAt(i, 0.0, -i), i);
            RaidEvent created = event(RaidEventType::RaidCreated, "Shoreline");
            created.raidMode = "Online";
            state.apply(created, i);
        }
    });

    for (int i = 0; i < 2000; ++i) {
        const auto snap = state.snapshot();
        // Whatever we caught, map id and normalized name must agree with each
        // other -- never one raid's id beside another raid's name.
        if (snap->mapId == "bigmap") {
            CHECK(snap->mapNormalized == "customs");
        } else if (snap->mapId == "Shoreline") {
            CHECK(snap->mapNormalized == "shoreline");
        }
        if (snap->fix.valid) {
            CHECK(snap->fix.world.x == doctest::Approx(-snap->fix.world.z));
        }
    }

    stop.store(true, std::memory_order_relaxed);
    writer.join();
}
