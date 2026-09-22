#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace tarkov::input {

/// Modifier flags. The values match Win32's MOD_* so the platform layer can
/// pass them straight to RegisterHotKey, but nothing here depends on Windows.
enum Modifier : unsigned {
    kNone = 0x0000,
    kAlt = 0x0001,
    kControl = 0x0002,
    kShift = 0x0004,
    kWin = 0x0008,
};

/// A key combination the user can rebind.
///
/// Stored and round-tripped as text ("Ctrl+Shift+M") so the settings file stays
/// readable and hand-editable.
struct Hotkey {
    unsigned modifiers = kNone;
    unsigned virtualKey = 0;  ///< Win32 virtual-key code.

    bool valid() const { return virtualKey != 0; }
    bool operator==(const Hotkey& other) const {
        return modifiers == other.modifiers && virtualKey == other.virtualKey;
    }
};

/// Parses "Ctrl+Shift+M", "Alt+F4", "F9", "`", "Num5", and "0x5B" for a key
/// with no name.
///
/// Case-insensitive, tolerant of spaces around '+', and accepts the common
/// aliases (Control/Ctrl, Escape/Esc, Win/Super/Meta). Returns nullopt when the
/// text names no key, so the settings UI can reject it rather than silently
/// binding nothing.
std::optional<Hotkey> parseHotkey(std::string_view text);

/// Renders a hotkey back to canonical text: modifiers in a fixed order, then
/// the key. parseHotkey(formatHotkey(h)) == h for every valid h.
std::string formatHotkey(const Hotkey& hotkey);

/// The default key that opens the map: F5.
///
/// Registered globally, so while the overlay runs it stops reaching every other
/// application. That is the price of a bare function key and the reason
/// --toggle-key exists.
Hotkey defaultToggleHotkey();

/// True when the combination is one we should refuse to bind: either it has no
/// key, or it is a system combination that will not reach us or will disrupt
/// the user's session.
bool isReservedCombination(const Hotkey& hotkey);

}  // namespace tarkov::input
