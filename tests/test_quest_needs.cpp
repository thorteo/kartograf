#include <doctest/doctest.h>

#include <string>

#include "core/data/TaskModel.h"
#include "fixtures.h"
#include "ui/QuestNeeds.h"
#include "ui/Strings.h"

using namespace tarkov;

namespace {

const data::TaskSet& fixtureTasks() {
    static const data::TaskSet set = data::parseTasks(
        test::readFile(test::fixturePath("tasks/tasks.json")),
        data::TaskLocale::parse(test::readFile(test::fixturePath("tasks/tasks_ru.json"))),
        data::TaskLocale::parse(test::readFile(test::fixturePath("tasks/items_ru.json"))));
    return set;
}

const data::Task& taskNamed(std::string_view normalizedName) {
    for (const auto& task : fixtureTasks().tasks) {
        if (task.normalizedName == normalizedName) {
            return task;
        }
    }
    FAIL("no task " << normalizedName);
    throw 0;
}

const data::TaskObjective& objectiveOf(const data::Task& task, std::string_view id) {
    for (const auto& objective : task.objectives) {
        if (objective.id == id) {
            return objective;
        }
    }
    FAIL("no objective " << id);
    throw 0;
}

/// The formatter's language for one test, restored afterwards.
struct Russian {
    ui::Lang saved = ui::language();
    Russian() { ui::setLanguage(ui::Lang::Ru); }
    ~Russian() { ui::setLanguage(saved); }
};

}  // namespace

TEST_CASE("a marker says what to bring to its place") {
    Russian ru;
    const auto& set = fixtureTasks();

    const auto& chumming = taskNamed("chumming");
    CHECK(ui::objectiveBringLines(objectiveOf(chumming, "5b4796c086f7745877352c2c"), set) ==
          std::vector<std::string>{"С собой: Золотая цепочка ×3"});

    const auto& wetJob = taskNamed("wet-job-part-2");
    CHECK(ui::objectiveBringLines(objectiveOf(wetJob, "5a281efe86f7745bc42005cc"), set) ==
          std::vector<std::string>{"С собой: Маркер MS2000"});

    const auto& airmail = taskNamed("airmail");
    CHECK(ui::objectiveBringLines(objectiveOf(airmail, "6672edebec0c3e2ad7d4e489"), set) ==
          std::vector<std::string>{
              "С собой, любой из: Реактивный сигнальный патрон РСП-30 (Красный), "
              "26x75 сигнальный патрон (Красный)"});
}

TEST_CASE("a key is named, but what to pick up is left to the description") {
    Russian ru;
    const auto& task = taskNamed("health-care-privacy-part-2");
    CHECK(ui::objectiveBringLines(objectiveOf(task, "5a68760f86f7743cc55d8709"),
                                  fixtureTasks()) ==
          std::vector<std::string>{"Ключ: Ключ от номера 306 западного крыла Санатория"});
}

TEST_CASE("what to hand over is listed, a long list of alternatives cut short") {
    Russian ru;
    const auto& set = fixtureTasks();

    // 52 kinds of medicine, any three of them, found in raid.
    const auto lines = ui::handInLines(taskNamed("first-in-line"), set);
    REQUIRE(lines.size() == 1);
    CHECK(lines[0].rfind("Сдать 3 шт., любые из: Таблетки-антибиотики Аугментин, ", 0) == 0);
    CHECK(lines[0].find("и ещё 48") != std::string::npos);
    CHECK(lines[0].find("(найдено в рейде)") != std::string::npos);

    // A quest item: the hard drive the quest had you find.
    CHECK(ui::handInLines(taskNamed("saving-the-mole"), set) ==
          std::vector<std::string>{"Сдать: Жесткий диск ученого TerraGroup"});

    // A quest with nothing to hand in.
    CHECK(ui::handInLines(taskNamed("wet-job-part-2"), set).empty());
}

TEST_CASE("without item names there is nothing to say, rather than raw ids") {
    const data::TaskSet bare = data::parseTasks(
        test::readFile(test::fixturePath("tasks/tasks.json")), data::TaskLocale{});
    for (const auto& task : bare.tasks) {
        CHECK(ui::handInLines(task, bare).empty());
        for (const auto& objective : task.objectives) {
            CHECK(ui::objectiveBringLines(objective, bare).empty());
        }
    }
}
