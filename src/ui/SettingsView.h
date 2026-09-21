#pragma once

#include <array>
#include <optional>
#include <string>

#include "core/input/Hotkey.h"
#include "core/settings/Settings.h"

namespace tarkov::ui {

/// Facts about the running system for the Status section.
///
/// This is where the question "is it broken, or did I just not press the
/// screenshot key?" gets answered, which nothing on the overlay itself can do
/// once everything there is hidden by default.
struct SettingsStatus {
    bool gameRunning = false;
    bool logFolderFound = false;
    bool screenshotFolderFound = false;
    bool questDataLoading = false;
    int questsLoaded = 0;
    int questsActive = 0;
    /// Seconds since the last position fix, or negative when there is none.
    double positionAgeSeconds = -1.0;
};

/// Hotkey slots, in the order the window lists them.
enum class HotkeySlot { Map = 0, Interact = 1, Hud = 2 };
constexpr int kHotkeySlotCount = 3;

/// What the settings window remembers between frames.
struct SettingsViewState {
    /// The slot waiting for a key press, if any.
    std::optional<HotkeySlot> capturing;
    /// Set by the platform when a key was pressed during capture. The view
    /// validates it on the next frame. An invalid Hotkey means "cancelled".
    std::optional<input::Hotkey> captured;
    /// Per-slot problem to show, e.g. another program owns the combination.
    std::array<std::string, kHotkeySlotCount> hotkeyErrors;
};

struct SettingsViewResult {
    bool changed = false;  ///< Settings were edited; save and apply them.
    bool done = false;     ///< The player closed the window with "Done".
    bool quit = false;     ///< The player asked to exit the program.
};

/// Draws the settings as a window filling the ImGui viewport.
SettingsViewResult drawSettings(settings::Settings& settings, SettingsViewState& state,
                                const SettingsStatus& status);

/// Validates a freshly captured key for a slot against the other slots.
/// Returns the problem to show, or an empty string if it can be used.
std::string validateCapturedKey(const settings::Settings& settings, HotkeySlot slot,
                                const input::Hotkey& key);

input::Hotkey& hotkeyFor(settings::Settings& settings, HotkeySlot slot);
const input::Hotkey& hotkeyFor(const settings::Settings& settings, HotkeySlot slot);

}  // namespace tarkov::ui
