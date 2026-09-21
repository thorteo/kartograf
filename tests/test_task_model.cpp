#include <doctest/doctest.h>

#include <algorithm>
#include <set>

#include "core/data/MapIds.h"
#include "core/data/TaskModel.h"
#include "fixtures.h"

using namespace tarkov::data;

namespace {

const TaskLocale& russian() {
    static const TaskLocale locale = TaskLocale::parse(
        tarkov::test::readFile(tarkov::test::fixturePath("tasks/tasks_ru.json")));
    return locale;
}

const TaskSet& fixtureTasks() {
    static const TaskSet set = parseTasks(
        tarkov::test::readFile(tarkov::test::fixturePath("tasks/tasks.json")),
        russian());
    return set;
}

/// Customs, as the API identifies it.
constexpr const char* kCustoms = "56f40101d2720b2a4d8b45d6";

}  // namespace

TEST_CASE("the real fixture parses into tasks with objectives") {
    const auto& set = fixtureTasks();
    REQUIRE(set.tasks.size() >= 10);

    for (const auto& task : set.tasks) {
        CAPTURE(task.id);
        CHECK_FALSE(task.id.empty());
        CHECK_FALSE(task.normalizedName.empty());
        CHECK_FALSE(task.objectives.empty());
        CHECK_FALSE(task.displayName().empty());
    }
}

TEST_CASE("names and descriptions come back as text, not translation keys") {
    // The API puts a key like "<id> name" in the name field. If the locale is
    // not applied, every quest in the overlay would be titled with a hex blob.
    const auto& set = fixtureTasks();

    int named = 0;
    for (const auto& task : set.tasks) {
        const std::string shown = task.displayName();
        CAPTURE(task.normalizedName);
        CAPTURE(shown);
        // A leaked key would end in " name" or be 24 hex characters.
        CHECK(shown.find(" name") == std::string::npos);
        CHECK_FALSE(std::all_of(shown.begin(), shown.end(), [](char c) {
            return std::isxdigit(static_cast<unsigned char>(c));
        }));
        if (!task.name.empty()) {
            ++named;
        }
    }
    CHECK(named >= 10);
}

TEST_CASE("Russian text survives the round trip") {
    const auto& locale = russian();
    CHECK(locale.size() > 40);
    // A known entry from the live locale endpoint.
    CHECK(locale.lookup("597a0f5686f774273b74f676 name") ==
          "\xd0\xa0\xd0\xb5\xd0\xb0\xd0\xb3\xd0\xb5\xd0\xbd\xd1\x82. "
          "\xd0\xa7\xd0\xb0\xd1\x81\xd1\x82\xd1\x8c 4");
}

TEST_CASE("a missing or blank translation falls back to a readable name") {
    // The live locale genuinely contains empty strings for entries nobody has
    // translated yet. That is not an error, and it must not surface as a blank
    // quest in the list.
    Task task;
    task.normalizedName = "health-care-privacy-part-2";
    task.name.clear();
    CHECK(task.displayName() == "Health Care Privacy Part 2");

    CHECK(prettifyNormalizedName("first-in-line") == "First In Line");
    CHECK(prettifyNormalizedName("saving-the-mole") == "Saving The Mole");
    CHECK(prettifyNormalizedName("") == "");
}

TEST_CASE("parsing without a locale still produces usable names") {
    const TaskLocale empty;
    const TaskSet set = parseTasks(
        tarkov::test::readFile(tarkov::test::fixturePath("tasks/tasks.json")), empty);

    REQUIRE_FALSE(set.tasks.empty());
    for (const auto& task : set.tasks) {
        CAPTURE(task.normalizedName);
        CHECK(task.name.empty());
        CHECK_FALSE(task.displayName().empty());
        CHECK(task.displayName().find(" name") == std::string::npos);
    }
}

TEST_CASE("objective zones carry real world coordinates") {
    const auto& set = fixtureTasks();

    int zoneCount = 0;
    for (const auto& task : set.tasks) {
        for (const auto& objective : task.objectives) {
            for (const auto& zone : objective.zones) {
                CAPTURE(task.normalizedName);
                CAPTURE(zone.id);
                CHECK_FALSE(zone.mapId.empty());
                // Tarkov maps span roughly +/-1100 units; anything outside that
                // means the coordinates are not what we think they are.
                CHECK(std::abs(zone.position.x) < 2000.0);
                CHECK(std::abs(zone.position.y) < 2000.0);
                CHECK(std::abs(zone.position.z) < 2000.0);
                ++zoneCount;
            }
        }
    }
    CHECK(zoneCount > 5);
}

TEST_CASE("a zone's elevation band places it on one floor, not all of them") {
    TaskZone banded;
    banded.top = 28.5;
    banded.bottom = 22.5;
    CHECK(banded.coversElevation(25.0));
    CHECK(banded.coversElevation(22.5));
    CHECK(banded.coversElevation(28.5));
    CHECK_FALSE(banded.coversElevation(10.0));
    CHECK_FALSE(banded.coversElevation(40.0));

    // A zone with no band applies everywhere rather than nowhere.
    TaskZone unbanded;
    CHECK(unbanded.coversElevation(-500.0));
    CHECK(unbanded.coversElevation(500.0));
}

TEST_CASE("quest item spawn points are all kept") {
    // The game picks one spawn per raid, so showing only the first would send
    // the player to the wrong place two times in three.
    const auto& set = fixtureTasks();

    int withLocations = 0;
    for (const auto& task : set.tasks) {
        for (const auto& objective : task.objectives) {
            for (const auto& location : objective.questItemLocations) {
                CHECK_FALSE(location.mapId.empty());
                CHECK_FALSE(location.positions.empty());
                ++withLocations;
            }
        }
    }
    CHECK(withLocations > 0);
}

TEST_CASE("required keys are flattened and de-duplicated") {
    const auto& set = fixtureTasks();
    for (const auto& task : set.tasks) {
        for (const auto& objective : task.objectives) {
            const auto& keys = objective.requiredKeyIds;
            CHECK(std::is_sorted(keys.begin(), keys.end()));
            CHECK(std::adjacent_find(keys.begin(), keys.end()) == keys.end());
        }
    }
}

TEST_CASE("filtering by map returns only tasks with a place on it") {
    const auto& set = fixtureTasks();
    const auto onCustoms = set.onMap(kCustoms);

    for (const auto* task : onCustoms) {
        CAPTURE(task->normalizedName);
        bool found = false;
        for (const auto& objective : task->objectives) {
            for (const auto& zone : objective.zones) {
                found = found || zone.mapId == kCustoms;
            }
            for (const auto& location : objective.questItemLocations) {
                found = found || location.mapId == kCustoms;
            }
        }
        CHECK(found);
    }

    // Results are ordered so the list does not reshuffle between frames.
    for (std::size_t i = 1; i < onCustoms.size(); ++i) {
        CHECK(onCustoms[i - 1]->displayName() <= onCustoms[i]->displayName());
    }

    // An id nobody uses yields nothing rather than everything.
    CHECK(set.onMap("not-a-map").empty());
    CHECK(set.onMap("").empty());
}

TEST_CASE("malformed input costs the quest layer, not the overlay") {
    const TaskLocale locale;
    for (const char* bad : {"", "{", "null", "[]", R"({"data":null})",
                            R"({"data":{"tasks":[]}})", R"({"nope":1})"}) {
        CAPTURE(bad);
        const TaskSet set = parseTasks(bad, locale);
        CHECK(set.tasks.empty());
    }

    for (const char* bad : {"", "{", "null", R"({"data":42})"}) {
        CAPTURE(bad);
        CHECK(TaskLocale::parse(bad).size() == 0);
    }
}

TEST_CASE("map ids resolve to the map that gets drawn") {
    const MapIdTable table = MapIdTable::parse(kMapIdsJson);
    REQUIRE(table.size() >= 17);

    CHECK(table.drawnAs("56f40101d2720b2a4d8b45d6") == "customs");
    CHECK(table.drawnAs("5704e554d2720bac5b8b456e") == "shoreline");
    CHECK(table.drawnAs("653e6760052c01c1c805532f") == "ground-zero");

    // Variants share a picture, so a quest on any of them belongs on the same
    // map. Night Factory quests vanishing on Factory would be a quiet bug.
    CHECK(table.drawnAs("59fc81d786f774390775787e") == "factory");
    CHECK(table.drawnAs("65b8d6f5cdde2479cb2a3125") == "ground-zero");
    CHECK(table.drawnAs("68236e8153654e8c1200798a") == "ground-zero");
    CHECK(table.drawnAs("6a294a5b5eb5f9a1700417b7") == "the-lab");

    // An unknown id must not resolve to some default map.
    CHECK_FALSE(table.drawnAs("0000000000000000000000ff").has_value());
    CHECK_FALSE(table.drawnAs("").has_value());
}

TEST_CASE("every drawable map's variant ids are found together") {
    const MapIdTable table = MapIdTable::parse(kMapIdsJson);

    const auto factory = table.idsFor("factory");
    CHECK(factory.size() == 2);  // day and night

    const auto groundZero = table.idsFor("ground-zero");
    CHECK(groundZero.size() == 3);  // base, level 21+, tutorial

    CHECK(table.idsFor("customs").size() == 1);
    CHECK(table.idsFor("nope").empty());
}

TEST_CASE("every map id in the task fixture is one we can place") {
    // A task on a map we cannot resolve would silently disappear.
    const MapIdTable table = MapIdTable::parse(kMapIdsJson);
    const auto& set = fixtureTasks();

    std::set<std::string> seen;
    for (const auto& task : set.tasks) {
        for (const auto& objective : task.objectives) {
            for (const auto& zone : objective.zones) {
                seen.insert(zone.mapId);
            }
            for (const auto& location : objective.questItemLocations) {
                seen.insert(location.mapId);
            }
        }
    }
    REQUIRE_FALSE(seen.empty());
    for (const auto& mapId : seen) {
        CAPTURE(mapId);
        CHECK(table.drawnAs(mapId).has_value());
    }
}

TEST_CASE("objective types map to the kind of place they mark") {
    CHECK(objectiveKind("visit") == ObjectiveKind::Visit);
    CHECK(objectiveKind("mark") == ObjectiveKind::Mark);
    CHECK(objectiveKind("plantItem") == ObjectiveKind::Plant);
    CHECK(objectiveKind("plantQuestItem") == ObjectiveKind::Plant);
    CHECK(objectiveKind("findQuestItem") == ObjectiveKind::Find);
    CHECK(objectiveKind("shoot") == ObjectiveKind::Shoot);
    CHECK(objectiveKind("useItem") == ObjectiveKind::Use);
    // Types with no places, and ones not invented yet, fall back.
    CHECK(objectiveKind("giveItem") == ObjectiveKind::Other);
    CHECK(objectiveKind("") == ObjectiveKind::Other);
}
