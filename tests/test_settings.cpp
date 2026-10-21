#include <doctest/doctest.h>

#include <cmath>
#include <limits>

#include <nlohmann/json.hpp>

#include "core/settings/Settings.h"

using namespace tarkov;
using settings::Settings;

TEST_CASE("defaults round-trip through the settings file") {
    const Settings defaults;
    CHECK(settings::parse(settings::serialize(defaults)) == defaults);
    CHECK(defaults.language == "ru");
    CHECK(input::formatHotkey(defaults.mapKey) == "F5");
    CHECK(defaults.showSettingsOnStart);
}

TEST_CASE("every field survives a round trip") {
    Settings s;
    s.mapKey = *input::parseHotkey("Ctrl+F6");
    s.interactKey = *input::parseHotkey("Alt+J");
    s.hudKey = *input::parseHotkey("F9");
    s.language = "en";
    s.mapOpacity = 0.55f;
    s.preferAbstractMap = false;
    s.showQuests = false;
    s.showExtracts = false;
    s.showTransits = false;
    s.showSpawns = true;
    s.showCaches = true;
    s.dimOtherFloors = false;
    s.showHud = true;
    s.excludeFromCapture = false;
    s.showSettingsOnStart = false;
    CHECK(settings::parse(settings::serialize(s)) == s);
}

TEST_CASE("one bad field does not reset the others") {
    // A hand-edit with a typo must cost that field only.
    std::vector<settings::Issue> issues;
    const Settings s = settings::parse(R"({
        "mapKey": "Ctrl+Nonsense",
        "mapOpacity": "very",
        "showSpawns": true,
        "language": "en"
    })", &issues);
    CHECK(input::formatHotkey(s.mapKey) == "F5");  // kept its default
    CHECK(s.mapOpacity == doctest::Approx(0.80f));
    CHECK(s.showSpawns);                           // the good fields applied
    CHECK(s.language == "en");
    CHECK(issues.size() == 2);
}

TEST_CASE("an unreadable file yields defaults and says so") {
    for (const char* bad : {"{", "null", "[]", "42"}) {
        CAPTURE(bad);
        std::vector<settings::Issue> issues;
        CHECK(settings::parse(bad, &issues) == Settings{});
        CHECK(issues.size() == 1);
    }
    std::vector<settings::Issue> none;
    CHECK(settings::parse("", &none) == Settings{});  // first run: no file, no complaint
    CHECK(none.empty());
}

TEST_CASE("sanitize repairs values that cannot be used") {
    Settings s;
    s.mapOpacity = 7.0f;
    s.language = "de";
    s.hudKey = *input::parseHotkey("Alt+F4");  // would close the game
    const auto changed = settings::sanitize(s);
    CHECK(s.mapOpacity == doctest::Approx(1.0f));
    CHECK(s.language == "ru");
    CHECK(input::formatHotkey(s.hudKey) == "Alt+H");
    CHECK(changed.size() == 3);

    Settings nan;
    nan.mapOpacity = std::numeric_limits<float>::quiet_NaN();
    settings::sanitize(nan);
    CHECK(nan.mapOpacity == doctest::Approx(0.80f));
}

TEST_CASE("two actions can never share a key") {
    // RegisterHotKey refuses a second registration of the same combination, so
    // a duplicate would leave one action with no working key at all.
    Settings s;
    s.interactKey = s.mapKey;
    const auto changed = settings::sanitize(s);
    CHECK_FALSE(s.interactKey == s.mapKey);
    CHECK_FALSE(changed.empty());

    Settings all;
    all.interactKey = *input::parseHotkey("Alt+H");  // collides with the HUD default
    all.hudKey = *input::parseHotkey("Alt+H");
    settings::sanitize(all);
    CHECK_FALSE(all.hudKey == all.interactKey);
}

TEST_CASE("valid settings pass through sanitize untouched") {
    Settings s;
    s.mapOpacity = 0.5f;
    s.mapKey = *input::parseHotkey("F6");
    const Settings before = s;
    CHECK(settings::sanitize(s).empty());
    CHECK(s == before);
}

TEST_CASE("only the player's edits are saved, not command-line overrides") {
    settings::Settings stored;           // what the file says
    settings::Settings running = stored;  // with --lang en --map-opacity 0.3
    running.language = "en";
    running.mapOpacity = 0.3f;

    // The player ticks one box in the settings window.
    const settings::Settings before = running;
    running.showSpawns = !running.showSpawns;

    CHECK(settings::applyEdits(stored, before, running));
    CHECK(stored.showSpawns == running.showSpawns);
    CHECK(stored.language == "ru");
    CHECK(stored.mapOpacity == doctest::Approx(0.80f));
}

TEST_CASE("every settings field is carried when edited") {
    // Every field the file holds, changed through the file format itself, so
    // a field added to serialize() but not to applyEdits() fails here.
    const settings::Settings before;
    auto json = nlohmann::json::parse(settings::serialize(before));
    char letter = 'A';
    for (auto& [key, value] : json.items()) {
        if (key == "version") {
            continue;
        }
        if (value.is_boolean()) {
            value = !value.get<bool>();
        } else if (value.is_number()) {
            value = 0.5;
        } else if (key == "language") {
            value = "en";
        } else {
            value = std::string("Ctrl+") + letter++;  // a hotkey
        }
    }
    std::vector<settings::Issue> issues;
    const settings::Settings after = settings::parse(json.dump(), &issues);
    REQUIRE(issues.empty());

    settings::Settings stored;
    CHECK(settings::applyEdits(stored, before, after));
    CHECK(stored == after);
    CHECK_FALSE(stored == before);
}
