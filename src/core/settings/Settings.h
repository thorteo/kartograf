#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "core/input/Hotkey.h"

namespace tarkov::settings {

/// Everything the player configures, persisted between runs.
///
/// Before this existed every option was a command-line flag and nothing
/// survived a restart. Command-line flags still exist for development and
/// override what is loaded here, but they are never written back.
struct Settings {
    input::Hotkey mapKey = input::defaultToggleHotkey();
    input::Hotkey interactKey{input::kAlt, 'N'};
    input::Hotkey hudKey{input::kAlt, 'H'};

    std::string language = "ru";  ///< "ru" or "en"

    float mapOpacity = 0.80f;
    bool preferAbstractMap = true;
    bool showQuests = true;
    bool showExtracts = true;
    bool showTransits = true;
    bool showSpawns = false;
    bool dimOtherFloors = true;

    bool showHud = false;
    bool excludeFromCapture = true;
    bool showSettingsOnStart = true;

    bool operator==(const Settings&) const = default;
};

/// Problems found while loading or validating, for the settings window to show.
struct Issue {
    std::string field;
    std::string detail;
};

/// Serialised for disk. Hotkeys are stored as text ("Alt+N") so the file stays
/// readable and hand-editable.
std::string serialize(const Settings& settings);

/// Parses a settings file. Missing or malformed fields keep their defaults, and
/// each problem is reported rather than failing the whole load: a hand-edit
/// with one typo must not reset every other preference.
Settings parse(std::string_view json, std::vector<Issue>* issues = nullptr);

/// Clamps and repairs values: opacity into range, unknown language to Russian,
/// and hotkeys that are reserved or collide with each other back to defaults.
/// Returns what was changed.
std::vector<Issue> sanitize(Settings& settings);

}  // namespace tarkov::settings
