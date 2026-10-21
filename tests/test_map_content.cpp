#include <doctest/doctest.h>

#include <set>

#include "core/data/MapFeatures.h"
#include "core/data/MapIds.h"
#include "core/data/QuestProgress.h"
#include "core/data/TaskModel.h"
#include "fixtures.h"
#include "ui/MapContent.h"
#include "ui/Strings.h"

using namespace tarkov;

namespace {

const data::MapIdTable& mapIds() {
    static const auto table = data::MapIdTable::parse(data::kMapIdsJson);
    return table;
}

const data::TaskSet& tasks() {
    static const auto set = data::parseTasks(
        test::readFile(test::fixturePath("tasks/tasks.json")),
        data::TaskLocale::parse(test::readFile(test::fixturePath("tasks/tasks_ru.json"))),
        data::TaskLocale::parse(test::readFile(test::fixturePath("tasks/items_ru.json"))));
    return set;
}

}  // namespace

TEST_CASE("map points come from every id that draws as the map") {
    ui::setLanguage(ui::Lang::Ru);
    const auto features = data::parseMapFeatures(
        test::readFile(test::fixturePath("maps/customs.json")), data::TaskLocale{});
    ui::MapOverlays overlays;
    overlays.extracts.resize(3);  // stale, from another map

    ui::buildMapPoints(features, mapIds(), "customs", overlays);
    CHECK(overlays.extracts.size() == 27);  // the fixture's count, stale ones gone
    CHECK(overlays.caches.size() == 65);
    for (const auto& cache : overlays.caches) {
        CHECK((cache.icon == ui::MarkerIcon::CacheBarrel ||
               cache.icon == ui::MarkerIcon::CacheGround));
        CHECK_FALSE(cache.label.empty());
    }
    CHECK_FALSE(overlays.spawns.empty());
    for (const auto& spawn : overlays.spawns) {
        CHECK((spawn.icon == ui::MarkerIcon::SpawnPmc ||
               spawn.icon == ui::MarkerIcon::SpawnBoss));
    }

    ui::buildMapPoints(features, mapIds(), "woods", overlays);
    CHECK(overlays.extracts.empty());  // no Woods data in the fixture
    CHECK(overlays.caches.empty());
}

TEST_CASE("quest places are filtered to active quests when progress is known") {
    const auto all = ui::buildQuestPlaces(tasks(), mapIds(), "customs", nullptr);
    REQUIRE_FALSE(all.empty());

    std::set<std::string> names;
    for (const auto& place : all) {
        names.insert(place.taskName);
    }
    REQUIRE(names.size() >= 2);

    // Mark one quest with places on Customs as the only active one.
    const data::Task* chosen = nullptr;
    for (const auto& task : tasks().tasks) {
        if (task.displayName() == *names.begin()) {
            chosen = &task;
        }
    }
    REQUIRE(chosen != nullptr);
    data::QuestProgress progress;
    progress.setManual(chosen->id, data::QuestState::Active, 1);
    const auto effective = data::resolveProgress(progress, tasks());

    const auto active = ui::buildQuestPlaces(tasks(), mapIds(), "customs", &effective);
    REQUIRE_FALSE(active.empty());
    CHECK(active.size() < all.size());
    for (const auto& place : active) {
        CHECK(place.taskName == chosen->displayName());
    }
}

TEST_CASE("the quest list covers every quest on the map, with its state") {
    data::QuestProgress progress;
    const auto places = ui::buildQuestPlaces(tasks(), mapIds(), "customs", nullptr);
    std::set<std::string> withPlaces;
    for (const auto& place : places) {
        withPlaces.insert(place.taskName);
    }

    const std::string someId = tasks().tasks.front().id;
    progress.setManual(someId, data::QuestState::Completed, 1);
    const auto effective = data::resolveProgress(progress, tasks());
    const auto entries =
        ui::buildQuestList(tasks(), mapIds(), "customs", effective, progress);

    std::set<std::string> listed;
    for (const auto& entry : entries) {
        listed.insert(entry.name);
        CHECK(entry.objectivesHere > 0);
        if (entry.id == someId) {
            CHECK(entry.manual);
            CHECK(entry.state == data::QuestState::Completed);
        }
    }
    CHECK(listed == withPlaces);
    CHECK(ui::buildQuestList(tasks(), mapIds(), "not-a-map", effective, progress).empty());
}
