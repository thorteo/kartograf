#include <doctest/doctest.h>

#include "imgui_harness.h"
#include "ui/SettingsView.h"

using namespace tarkov;
using ui::HotkeySlot;

TEST_CASE("a captured key is applied to the slot that asked for it") {
    test::HeadlessImGui imgui(640.0f, 720.0f);
    settings::Settings s;
    ui::SettingsViewState state;
    state.capturing = HotkeySlot::Interact;
    state.captured = *input::parseHotkey("Ctrl+J");

    ui::SettingsViewResult result;
    imgui.frame([&] { result = ui::drawSettings(s, state, {}); });

    CHECK(result.changed);
    CHECK(input::formatHotkey(s.interactKey) == "Ctrl+J");
    CHECK_FALSE(state.capturing.has_value());
}

TEST_CASE("a key already used elsewhere is refused with a reason") {
    test::HeadlessImGui imgui(640.0f, 720.0f);
    settings::Settings s;
    ui::SettingsViewState state;
    state.capturing = HotkeySlot::Hud;
    state.captured = s.mapKey;  // F5, already the map key

    ui::SettingsViewResult result;
    imgui.frame([&] { result = ui::drawSettings(s, state, {}); });

    CHECK_FALSE(result.changed);
    CHECK(input::formatHotkey(s.hudKey) == "Alt+H");
    CHECK_FALSE(state.hotkeyErrors[static_cast<int>(HotkeySlot::Hud)].empty());
}

TEST_CASE("a reserved key is refused") {
    settings::Settings s;
    CHECK_FALSE(ui::validateCapturedKey(s, HotkeySlot::Map, *input::parseHotkey("Alt+F4"))
                    .empty());
    CHECK(ui::validateCapturedKey(s, HotkeySlot::Map, *input::parseHotkey("F6")).empty());
    // Rebinding a slot to its own current key is not a conflict with itself.
    CHECK(ui::validateCapturedKey(s, HotkeySlot::Map, s.mapKey).empty());
}

TEST_CASE("cancelling capture changes nothing") {
    test::HeadlessImGui imgui(640.0f, 720.0f);
    settings::Settings s;
    const settings::Settings before = s;
    ui::SettingsViewState state;
    state.capturing = HotkeySlot::Map;
    state.captured = input::Hotkey{};  // Esc

    ui::SettingsViewResult result;
    imgui.frame([&] { result = ui::drawSettings(s, state, {}); });
    CHECK_FALSE(result.changed);
    CHECK(s == before);
    CHECK_FALSE(state.capturing.has_value());
}

TEST_CASE("the window draws with and without status data") {
    test::HeadlessImGui imgui(640.0f, 720.0f);
    settings::Settings s;
    ui::SettingsViewState state;
    ui::SettingsStatus status;
    imgui.frame([&] { ui::drawSettings(s, state, status); });

    status.gameRunning = true;
    status.logFolderFound = true;
    status.screenshotFolderFound = true;
    status.questsLoaded = 515;
    status.questsActive = 46;
    status.positionAgeSeconds = 12.0;
    ui::SettingsViewResult result;
    imgui.frame([&] { result = ui::drawSettings(s, state, status); });
    CHECK_FALSE(result.changed);
    CHECK_FALSE(result.done);
    CHECK_FALSE(result.quit);
}
