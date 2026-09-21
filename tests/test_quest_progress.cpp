#include <doctest/doctest.h>

#include <atomic>
#include <thread>

#include "core/data/QuestProgress.h"
#include "fixtures.h"

using namespace tarkov;
using data::QuestProgress;
using data::QuestState;
using game::QuestEvent;
using game::QuestEventKind;

namespace {

QuestEvent ev(QuestEventKind kind, const char* id, std::int64_t at) {
    return QuestEvent{kind, id, at};
}

/// A small quest chain: c requires b, b requires a. d stands alone.
data::TaskSet chain() {
    data::TaskSet set;
    auto task = [](const char* id, std::vector<std::string> prerequisites) {
        data::Task t;
        t.id = id;
        t.normalizedName = id;
        t.prerequisiteIds = std::move(prerequisites);
        return t;
    };
    set.tasks.push_back(task("a", {}));
    set.tasks.push_back(task("b", {"a"}));
    set.tasks.push_back(task("c", {"b"}));
    set.tasks.push_back(task("d", {}));
    return set;
}

}  // namespace

TEST_CASE("a quest moves through its lifecycle in time order") {
    QuestProgress progress;
    CHECK(progress.apply(ev(QuestEventKind::Started, "q", 100)));
    CHECK(progress.find("q")->state == QuestState::Active);
    CHECK(progress.apply(ev(QuestEventKind::Completed, "q", 200)));
    CHECK(progress.find("q")->state == QuestState::Completed);
}

TEST_CASE("importing an older log cannot roll progress back") {
    // Logs are imported in whatever order the folders are found. An old
    // "started" read after a newer "completed" must not reopen the quest.
    QuestProgress progress;
    progress.apply(ev(QuestEventKind::Completed, "q", 200));
    CHECK_FALSE(progress.apply(ev(QuestEventKind::Started, "q", 100)));
    CHECK(progress.find("q")->state == QuestState::Completed);
}

TEST_CASE("a start and a completion in the same second resolve to completed") {
    QuestProgress progress;
    progress.apply(ev(QuestEventKind::Completed, "q", 100));
    progress.apply(ev(QuestEventKind::Started, "q", 100));
    CHECK(progress.find("q")->state == QuestState::Completed);

    QuestProgress other;
    other.apply(ev(QuestEventKind::Started, "q", 100));
    other.apply(ev(QuestEventKind::Completed, "q", 100));
    CHECK(other.find("q")->state == QuestState::Completed);
}

TEST_CASE("a manual correction stands until the game says something newer") {
    QuestProgress progress;
    progress.apply(ev(QuestEventKind::Started, "q", 100));
    progress.setManual("q", QuestState::Completed, 150);
    CHECK(progress.find("q")->manual);

    // Re-reading the same old log must not undo the player's correction.
    progress.apply(ev(QuestEventKind::Started, "q", 100));
    CHECK(progress.find("q")->state == QuestState::Completed);

    // Something genuinely new from the game does win.
    progress.apply(ev(QuestEventKind::Failed, "q", 300));
    CHECK(progress.find("q")->state == QuestState::Failed);
    CHECK_FALSE(progress.find("q")->manual);
}

TEST_CASE("clearing a correction only removes manual records") {
    QuestProgress progress;
    progress.apply(ev(QuestEventKind::Started, "from-log", 100));
    progress.setManual("by-hand", QuestState::Completed, 100);

    progress.clearManual("from-log");
    progress.clearManual("by-hand");
    CHECK(progress.find("from-log") != nullptr);
    CHECK(progress.find("by-hand") == nullptr);
}

TEST_CASE("prerequisites of a started quest are inferred as completed") {
    // The game keeps logs for about five days. Starting c proves a and b were
    // done, even if those completions scrolled out of the logs long ago.
    QuestProgress progress;
    progress.apply(ev(QuestEventKind::Started, "c", 100));

    const auto effective = data::resolveProgress(progress, chain());
    CHECK(effective.stateOf("c") == QuestState::Active);
    CHECK(effective.stateOf("b") == QuestState::Completed);
    CHECK(effective.stateOf("a") == QuestState::Completed);
    CHECK(effective.stateOf("d") == QuestState::Unknown);

    // And the UI can tell these apart from completions it actually saw.
    CHECK(effective.inferredCompleted.count("a") == 1);
    CHECK(effective.inferredCompleted.count("b") == 1);
    CHECK(effective.inferredCompleted.count("c") == 0);
}

TEST_CASE("an explicit record beats inference") {
    // Starting c implies b was completed -- unless the player has said
    // otherwise. Their record stands and is not overwritten.
    QuestProgress progress;
    progress.apply(ev(QuestEventKind::Started, "c", 100));
    progress.setManual("b", QuestState::Failed, 50);

    const auto effective = data::resolveProgress(progress, chain());
    CHECK(effective.stateOf("b") == QuestState::Failed);
    CHECK(effective.inferredCompleted.count("b") == 0);
}

TEST_CASE("an active quest is itself evidence about its prerequisites") {
    // Marking b as active by hand still implies a was done: b cannot have been
    // taken otherwise. The correction is respected -- b stays active rather
    // than being promoted to completed -- and inference continues through it.
    QuestProgress progress;
    progress.apply(ev(QuestEventKind::Started, "c", 100));
    progress.setManual("b", QuestState::Active, 50);

    const auto effective = data::resolveProgress(progress, chain());
    CHECK(effective.stateOf("b") == QuestState::Active);
    CHECK(effective.stateOf("a") == QuestState::Completed);
}

TEST_CASE("inference survives cycles and unknown ids") {
    data::TaskSet set = chain();
    set.tasks[0].prerequisiteIds = {"c"};  // a -> c -> b -> a: a cycle

    QuestProgress progress;
    progress.apply(ev(QuestEventKind::Started, "c", 1));
    progress.apply(ev(QuestEventKind::Started, "not-in-data", 1));

    const auto effective = data::resolveProgress(progress, set);  // must terminate
    CHECK(effective.stateOf("not-in-data") == QuestState::Active);
    CHECK(effective.stateOf("b") == QuestState::Completed);
}

TEST_CASE("progress round-trips through its file format") {
    QuestProgress progress;
    progress.apply(ev(QuestEventKind::Started, "5936d90786f7742b1420ba5b", 100));
    progress.apply(ev(QuestEventKind::Completed, "59674cd986f7744ab26e32f2", 200));
    progress.setManual("5936da9e86f7742d65037edf", QuestState::Completed, 300);

    const QuestProgress reloaded = QuestProgress::parse(progress.serialize());
    REQUIRE(reloaded.records().size() == 3);
    for (const auto& [id, record] : progress.records()) {
        CAPTURE(id);
        const auto* back = reloaded.find(id);
        REQUIRE(back != nullptr);
        CHECK(back->state == record.state);
        CHECK(back->since == record.since);
        CHECK(back->manual == record.manual);
    }
}

TEST_CASE("a damaged progress file loads as empty rather than crashing") {
    for (const char* bad : {"", "{", "null", "[]", R"({"quests":5})",
                            R"({"quests":{"x":{"state":"sideways"}}})"}) {
        CAPTURE(bad);
        CHECK(QuestProgress::parse(bad).records().empty());
    }
}

TEST_CASE("the real log fixture produces a consistent progress picture") {
    const auto lines = test::splitLines(
        test::readFile(test::fixturePath("logs/quest-notifications.log")));
    game::QuestEventParser parser;
    QuestProgress progress;
    for (const auto& line : lines) {
        if (auto event = parser.feedLine(line)) {
            progress.apply(*event);
        }
    }
    int active = 0, completed = 0;
    for (const auto& [id, record] : progress.records()) {
        active += record.state == QuestState::Active;
        completed += record.state == QuestState::Completed;
        CHECK_FALSE(record.manual);
    }
    CHECK(active > 0);
    CHECK(completed > 0);
}

TEST_CASE("the store publishes coherent snapshots across threads") {
    data::QuestProgressStore store;
    std::atomic<bool> stop{false};
    std::thread writer([&] {
        for (int i = 1; !stop.load(); ++i) {
            store.apply(ev(QuestEventKind::Started, "q", i));
            store.apply(ev(QuestEventKind::Completed, "q", i));
        }
    });
    std::uint64_t lastRevision = 0;
    for (int i = 0; i < 2000; ++i) {
        const auto snapshot = store.snapshot();
        REQUIRE(snapshot != nullptr);
        if (const auto* record = snapshot->find("q")) {
            CHECK((record->state == QuestState::Active ||
                   record->state == QuestState::Completed));
        }
        const auto revision = store.revision();
        CHECK(revision >= lastRevision);
        lastRevision = revision;
    }
    stop.store(true);
    writer.join();
}
