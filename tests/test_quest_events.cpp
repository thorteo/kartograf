#include <doctest/doctest.h>

#include <map>
#include <vector>

#include "core/game/QuestEvents.h"
#include "fixtures.h"

using namespace tarkov::game;

namespace {

std::vector<QuestEvent> fixtureEvents() {
    const auto lines = tarkov::test::splitLines(tarkov::test::readFile(
        tarkov::test::fixturePath("logs/quest-notifications.log")));
    QuestEventParser parser;
    std::vector<QuestEvent> events;
    for (const auto& line : lines) {
        if (auto event = parser.feedLine(line)) {
            events.push_back(std::move(*event));
        }
    }
    return events;
}

}  // namespace

TEST_CASE("recognises quest starts and completions in the real log") {
    const auto events = fixtureEvents();
    REQUIRE(events.size() == 14);

    std::map<QuestEventKind, int> counts;
    for (const auto& event : events) {
        ++counts[event.kind];
        CAPTURE(event.questId);
        CHECK(event.questId.size() == 24);
        CHECK(event.unixTime > 1700000000);  // a plausible 2020s timestamp
    }
    CHECK(counts[QuestEventKind::Started] == 11);
    CHECK(counts[QuestEventKind::Completed] == 3);
}

TEST_CASE("a completion is not mistaken for a start because of its text") {
    // The trap in this format: completion messages say "quest started" in
    // their text field. Only the type and the templateId suffix are reliable.
    const char* completion = R"({
      "type": "new_message",
      "message": {
        "type": 12,
        "dt": 1789862590,
        "text": "quest started",
        "templateId": "5936d90786f7742b1420ba5b successMessageText"
      }
    })";
    const auto event = parseQuestNotification(completion);
    REQUIRE(event.has_value());
    CHECK(event->kind == QuestEventKind::Completed);
    CHECK(event->questId == "5936d90786f7742b1420ba5b");
    CHECK(event->unixTime == 1789862590);
}

TEST_CASE("the suffix after the quest id may carry extra words") {
    // Seen in the wild: "<id> successMessageText <otherId> 0".
    const auto event = parseQuestNotification(R"({"message":{"type":12,"dt":1,
        "templateId":"5936d90786f7742b1420ba5b successMessageText 63a0b2eabea67a6d93009e52 0"}})");
    REQUIRE(event.has_value());
    CHECK(event->kind == QuestEventKind::Completed);
    CHECK(event->questId == "5936d90786f7742b1420ba5b");
}

TEST_CASE("failures are recognised") {
    const auto event = parseQuestNotification(R"({"message":{"type":11,"dt":5,
        "templateId":"5936d90786f7742b1420ba5b failMessageText"}})");
    REQUIRE(event.has_value());
    CHECK(event->kind == QuestEventKind::Failed);
}

TEST_CASE("unrelated notifications are ignored") {
    const char* ignored[] = {
        R"({"type":"userMatchCreated","profileToken":"<TOKEN>"})",
        R"({"message":{"type":1,"templateId":"5936d90786f7742b1420ba5b description"}})",
        R"({"message":{"type":10,"templateId":"not-a-quest-id description"}})",
        R"({"message":{"type":10,"templateId":"5936d90786f7742b1420ba5b"}})",
        R"({"message":{"type":10,"templateId":"5936d90786f7742b1420ba5b successMessageText"}})",
        R"({"message":{"type":10}})",
        "", "{", "null",
    };
    for (const char* text : ignored) {
        CAPTURE(text);
        CHECK_FALSE(parseQuestNotification(text).has_value());
    }
}

TEST_CASE("a record interrupted by the next one is dropped, not misread") {
    QuestEventParser parser;
    CHECK_FALSE(parser.feedLine("2026-09-18 23:18:39.621|1.1.5.1.47510|Info|"
                                "push-notifications|Got notification | ChatMessageReceived\r"));
    CHECK_FALSE(parser.feedLine("{\r"));
    CHECK_FALSE(parser.feedLine("  \"message\": {\r"));
    // A new record starts before this one closed.
    CHECK_FALSE(parser.feedLine("2026-09-18 23:18:40.000|1.1.5.1.47510|Info|"
                                "push-notifications|LongPollingWebSocketRequest canceled\r"));
    CHECK_FALSE(parser.feedLine("}\r"));
}

TEST_CASE("braces inside strings do not end a record early") {
    QuestEventParser parser;
    parser.feedLine("2026-09-18 23:18:39.621|1.1.5.1.47510|Info|push-notifications|"
                    "Got notification | ChatMessageReceived");
    CHECK_FALSE(parser.feedLine("{\"note\": \"a } brace\","));
    CHECK_FALSE(parser.feedLine(" \"message\": {\"type\": 10, \"dt\": 3,"));
    const auto event = parser.feedLine(
        " \"templateId\": \"5936d90786f7742b1420ba5b description\"}}");
    REQUIRE(event.has_value());
    CHECK(event->kind == QuestEventKind::Started);
}
