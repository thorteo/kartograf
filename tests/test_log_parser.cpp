#include <doctest/doctest.h>

#include <map>

#include "core/game/LogParser.h"
#include "core/geo/MapConfig.h"
#include "fixtures.h"

using namespace tarkov;
using game::LogParser;
using game::RaidEventType;

namespace {

std::vector<game::RaidEvent> eventsFromFixture() {
    const auto lines = test::splitLines(test::readFile(
        test::fixturePath("logs/raid-lifecycle.log")));
    LogParser parser;
    std::vector<game::RaidEvent> events;
    for (const auto& line : lines) {
        if (auto event = parser.feedLine(line)) {
            events.push_back(std::move(*event));
        }
    }
    return events;
}

}  // namespace

TEST_CASE("splits a real log line into its fields") {
    const auto line = game::parseLogLine(
        "2026-09-20 01:36:09.808|1.1.5.1.47510|Info|application|"
        "scene preset path:maps/customs_preset.bundle rcid:bigmap.scenespreset.asset\r");

    REQUIRE(line.has_value());
    CHECK(line->timestamp.year == 2026);
    CHECK(line->timestamp.month == 9);
    CHECK(line->timestamp.day == 20);
    CHECK(line->timestamp.hour == 1);
    CHECK(line->timestamp.minute == 36);
    CHECK(line->timestamp.second == 9);
    CHECK(line->timestamp.millis == 808);
    CHECK(line->version == "1.1.5.1.47510");
    CHECK(line->level == "Info");
    CHECK(line->channel == "application");
    CHECK(line->message.find("scene preset path") == 0);
}

TEST_CASE("keeps pipes that belong to the message") {
    // output_000.log nests a second channel inside the message field, so a
    // naive split-on-every-pipe loses the payload.
    const auto line = game::parseLogLine(
        "2026-09-20 01:36:40.232|1.1.5.1.47510|Info|output|application|"
        "LocationLoaded:22.03 real:31.8 diff:9.76");

    REQUIRE(line.has_value());
    CHECK(line->channel == "output");
    CHECK(line->message == "application|LocationLoaded:22.03 real:31.8 diff:9.76");
}

TEST_CASE("treats a line without a timestamp as a continuation") {
    CHECK_FALSE(game::parseLogLine("  \"Language\": \"ru\",").has_value());
    CHECK_FALSE(game::parseLogLine("{").has_value());
    CHECK_FALSE(game::parseLogLine("").has_value());
}

TEST_CASE("recognises the full raid lifecycle in the real session") {
    const auto events = eventsFromFixture();

    std::map<RaidEventType, int> counts;
    for (const auto& event : events) {
        ++counts[event.type];
    }

    // The fixture session contains eight raids, and each one produced a map
    // load, a server assignment and a start.
    CHECK(counts[RaidEventType::MapLoading] == 8);
    CHECK(counts[RaidEventType::RaidCreated] == 8);
    CHECK(counts[RaidEventType::RaidStarted] == 8);
    CHECK(counts[RaidEventType::LocationLoaded] == 8);
    CHECK(counts[RaidEventType::MatchingStage] > 0);
}

TEST_CASE("extracts map, mode and raid code from the server assignment") {
    const auto events = eventsFromFixture();

    std::vector<const game::RaidEvent*> created;
    for (const auto& event : events) {
        if (event.type == RaidEventType::RaidCreated) {
            created.push_back(&event);
        }
    }
    REQUIRE(created.size() == 8);

    for (const auto* event : created) {
        CAPTURE(event->mapId);
        CHECK_FALSE(event->mapId.empty());
        CHECK(event->raidMode == "Online");
        CHECK(event->shortId == "<SHORT_ID>");  // scrubbed in the fixture
    }

    CHECK(created.front()->mapId == "bigmap");
    CHECK(created[6]->mapId == "Shoreline");
    CHECK(created[7]->mapId == "Sandbox");
}

TEST_CASE("map ids resolve despite inconsistent capitalisation") {
    // The same session writes "bigmap" lowercase and "Shoreline" capitalised.
    // This is the bug that would otherwise appear on exactly one map, live.
    using geo::mapIdToNormalizedName;

    CHECK(mapIdToNormalizedName("bigmap") == "customs");
    CHECK(mapIdToNormalizedName("BigMap") == "customs");
    CHECK(mapIdToNormalizedName("Shoreline") == "shoreline");
    CHECK(mapIdToNormalizedName("shoreline") == "shoreline");
    CHECK(mapIdToNormalizedName("Sandbox") == "ground-zero");
    CHECK(mapIdToNormalizedName("sandbox") == "ground-zero");
    CHECK(mapIdToNormalizedName("TarkovStreets") == "streets-of-tarkov");

    // An unknown id must not silently resolve to some default map.
    CHECK_FALSE(mapIdToNormalizedName("not_a_map").has_value());
    CHECK_FALSE(mapIdToNormalizedName("").has_value());
}

TEST_CASE("every map id seen in the fixture is one we can translate") {
    for (const auto& event : eventsFromFixture()) {
        if (event.mapId.empty()) {
            continue;
        }
        CAPTURE(event.mapId);
        CHECK(geo::mapIdToNormalizedName(event.mapId).has_value());
    }
}

TEST_CASE("timestamps order correctly across the session") {
    const auto events = eventsFromFixture();
    REQUIRE(events.size() > 2);

    long long previous = events.front().timestamp.millisSinceEpoch();
    for (const auto& event : events) {
        const long long current = event.timestamp.millisSinceEpoch();
        CHECK(current >= previous);
        previous = current;
    }
}

TEST_CASE("session folder names parse, with the hour not zero-padded") {
    const auto late = game::parseLogFolderName("log_2026.09.21_19-54-34_1.1.5.1.47510");
    REQUIRE(late);
    CHECK(late->year == 2026);
    CHECK(late->month == 9);
    CHECK(late->day == 21);
    CHECK(late->hour == 19);
    CHECK(late->minute == 54);
    CHECK(late->second == 34);

    const auto early = game::parseLogFolderName("log_2026.09.21_2-31-42_1.1.5.1.47510");
    REQUIRE(early);
    CHECK(early->hour == 2);

    // The reason this exists: as strings, the 02:31 session sorts after 19:54.
    CHECK(std::string("log_2026.09.21_2-31-42") > std::string("log_2026.09.21_19-54-34"));
    CHECK(late->millisSinceEpoch() > early->millisSinceEpoch());
}

TEST_CASE("the newest of the real session folders is found by time") {
    // Every session folder on the development machine on 2026-09-21.
    const char* names[] = {
        "log_2026.09.16_20-43-44_1.1.5.1.47510", "log_2026.09.16_21-21-16_1.1.5.1.47510",
        "log_2026.09.16_22-42-03_1.1.5.1.47510", "log_2026.09.18_0-56-52_1.1.5.1.47510",
        "log_2026.09.18_22-32-24_1.1.5.1.47510", "log_2026.09.19_4-10-34_1.1.5.1.47510",
        "log_2026.09.20_1-32-29_1.1.5.1.47510",  "log_2026.09.21_19-54-34_1.1.5.1.47510",
        "log_2026.09.21_2-31-42_1.1.5.1.47510",
    };
    std::string newest;
    long long newestTime = 0;
    for (const char* name : names) {
        const auto time = game::parseLogFolderName(name);
        REQUIRE(time);
        if (time->millisSinceEpoch() > newestTime) {
            newestTime = time->millisSinceEpoch();
            newest = name;
        }
    }
    CHECK(newest == "log_2026.09.21_19-54-34_1.1.5.1.47510");
}

TEST_CASE("names that are not session folders are rejected") {
    CHECK_FALSE(game::parseLogFolderName(""));
    CHECK_FALSE(game::parseLogFolderName("log_"));
    CHECK_FALSE(game::parseLogFolderName("logs_2026.09.21_19-54-34_1"));
    CHECK_FALSE(game::parseLogFolderName("log_2026.09.21"));
    CHECK_FALSE(game::parseLogFolderName("log_2026.13.21_19-54-34_1"));
    CHECK_FALSE(game::parseLogFolderName("log_2026.09.21_24-00-00_1"));
    CHECK_FALSE(game::parseLogFolderName("log_2026.09.21_19-54-34"));  // no version
}
