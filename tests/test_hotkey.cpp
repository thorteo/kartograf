#include <doctest/doctest.h>

#include "core/input/Hotkey.h"

using namespace tarkov::input;

TEST_CASE("parses the combinations a user would actually type") {
    struct Case {
        const char* text;
        unsigned modifiers;
        unsigned key;
    };
    const Case cases[] = {
        {"F9", kNone, 0x78},
        {"Alt+M", kAlt, 'M'},
        {"Ctrl+Shift+M", kControl | kShift, 'M'},
        {"Control+Shift+M", kControl | kShift, 'M'},
        {"ctrl+shift+m", kControl | kShift, 'M'},
        {"CTRL + SHIFT + M", kControl | kShift, 'M'},
        {"Win+O", kWin, 'O'},
        {"Super+O", kWin, 'O'},
        {"Ctrl+F12", kControl, 0x7B},  // VK_F1 is 0x70, so F12 is 0x7B
        {"`", kNone, 0xC0},
        {"Shift+5", kShift, '5'},
        {"Esc", kNone, 0x1B},
        {"Escape", kNone, 0x1B},
    };
    for (const auto& testCase : cases) {
        CAPTURE(testCase.text);
        const auto parsed = parseHotkey(testCase.text);
        REQUIRE(parsed.has_value());
        CHECK(parsed->modifiers == testCase.modifiers);
        CHECK(parsed->virtualKey == testCase.key);
    }
}

TEST_CASE("rejects text that names no key") {
    // A binding that silently resolves to nothing is worse than a rejected one:
    // the user presses their key in a raid and nothing happens.
    const char* rejected[] = {"", "   ", "Ctrl", "Ctrl+Shift", "+", "Ctrl+Nope",
                              "F0", "F25", "Ctrl+A+B"};
    for (const char* text : rejected) {
        CAPTURE(text);
        CHECK_FALSE(parseHotkey(text).has_value());
    }
}

TEST_CASE("formatting round-trips through parsing") {
    const char* inputs[] = {"F9",  "Alt+M",      "Ctrl+Shift+M", "Win+O",
                            "`",   "Ctrl+F12",   "Shift+5",      "Ctrl+Alt+Shift+Win+K"};
    for (const char* text : inputs) {
        CAPTURE(text);
        const auto parsed = parseHotkey(text);
        REQUIRE(parsed.has_value());

        const std::string formatted = formatHotkey(*parsed);
        const auto reparsed = parseHotkey(formatted);
        REQUIRE(reparsed.has_value());
        CHECK(*reparsed == *parsed);
    }
}

TEST_CASE("formatting is canonical regardless of input order") {
    const auto a = parseHotkey("Shift+Ctrl+Alt+K");
    const auto b = parseHotkey("Alt+Shift+Ctrl+K");
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK(formatHotkey(*a) == formatHotkey(*b));
    CHECK(formatHotkey(*a) == "Ctrl+Alt+Shift+K");
}

TEST_CASE("the default toggle is bindable") {
    const auto toggle = defaultToggleHotkey();
    CHECK(toggle.valid());
    CHECK_FALSE(isReservedCombination(toggle));
    CHECK(formatHotkey(toggle) == "F5");
    CHECK(toggle.modifiers == kNone);

    // It has to survive a round trip through the settings text, since that is
    // how a rebinding will be stored.
    const auto reparsed = parseHotkey(formatHotkey(toggle));
    REQUIRE(reparsed.has_value());
    CHECK(*reparsed == toggle);
}

TEST_CASE("refuses combinations that would not reach us or would hurt") {
    const char* reserved[] = {
        "Ctrl+Alt+Delete",  // intercepted by Windows
        "Win+L",            // locks the session
        "Alt+F4",           // closes the game
        "Alt+Tab",          // the user needs this mid-raid
    };
    for (const char* text : reserved) {
        CAPTURE(text);
        const auto parsed = parseHotkey(text);
        REQUIRE(parsed.has_value());
        CHECK(isReservedCombination(*parsed));
    }

    // Ordinary combinations stay allowed.
    for (const char* text : {"Alt+M", "Ctrl+Shift+M", "F9", "`"}) {
        CAPTURE(text);
        CHECK_FALSE(isReservedCombination(*parseHotkey(text)));
    }
}
