#include <doctest/doctest.h>

#include "core/game/LineSplitter.h"

using tarkov::game::LineSplitter;

TEST_CASE("a line split across reads comes out whole, once") {
    LineSplitter splitter;
    CHECK(splitter.feed("2026-09-20 01:36:09.808|1.1|Info|app").empty());
    CHECK(splitter.pending() > 0);
    const auto lines = splitter.feed("lication|GameStarted\r\nnext");
    REQUIRE(lines.size() == 1);
    CHECK(lines[0] == "2026-09-20 01:36:09.808|1.1|Info|application|GameStarted\r");
    CHECK(splitter.pending() == 4);  // "next", until its newline
}

TEST_CASE("several lines in one read, empty ones included") {
    LineSplitter splitter;
    const auto lines = splitter.feed("a\r\n\nb\n");
    REQUIRE(lines.size() == 3);
    CHECK(lines[0] == "a\r");
    CHECK(lines[1].empty());
    CHECK(lines[2] == "b");
    CHECK(splitter.pending() == 0);
}

TEST_CASE("a byte at a time gives the same lines") {
    const std::string text = "first\r\nsecond\r\n{\n  \"json\": 1\n}\r\n";
    LineSplitter whole;
    const auto expected = whole.feed(text);

    LineSplitter trickle;
    std::vector<std::string> got;
    for (const char c : text) {
        for (auto& line : trickle.feed(std::string_view(&c, 1))) {
            got.push_back(std::move(line));
        }
    }
    CHECK(got == expected);
    CHECK(got.size() == 5);
}

TEST_CASE("reset drops a partial line") {
    LineSplitter splitter;
    splitter.feed("half a li");
    splitter.reset();
    const auto lines = splitter.feed("ne\n");
    REQUIRE(lines.size() == 1);
    CHECK(lines[0] == "ne");
}
