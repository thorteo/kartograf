#include "core/input/Hotkey.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <vector>

namespace tarkov::input {
namespace {

std::string toLower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
        s.remove_prefix(1);
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.remove_suffix(1);
    }
    return s;
}

struct NamedKey {
    std::string_view name;
    unsigned vk;
};

// Win32 virtual-key codes, spelled out so this file needs no Windows header.
// Names are written as they are shown and matched without regard to case.
// Where one key has several, the first multi-letter one is what gets written.
constexpr NamedKey kNamedKeys[] = {
    {"Backspace", 0x08}, {"Tab", 0x09},    {"Enter", 0x0D},  {"Return", 0x0D},
    {"Pause", 0x13},     {"CapsLock", 0x14}, {"Esc", 0x1B},  {"Escape", 0x1B},
    {"Space", 0x20},     {"PageUp", 0x21}, {"PageDown", 0x22}, {"End", 0x23},
    {"Home", 0x24},      {"Left", 0x25},   {"Up", 0x26},     {"Right", 0x27},
    {"Down", 0x28},      {"PrintScreen", 0x2C}, {"Insert", 0x2D}, {"Delete", 0x2E},
    {"Del", 0x2E},
    {"Num0", 0x60}, {"Num1", 0x61}, {"Num2", 0x62}, {"Num3", 0x63}, {"Num4", 0x64},
    {"Num5", 0x65}, {"Num6", 0x66}, {"Num7", 0x67}, {"Num8", 0x68}, {"Num9", 0x69},
    {"NumMultiply", 0x6A}, {"NumAdd", 0x6B}, {"NumSubtract", 0x6D},
    {"NumDecimal", 0x6E}, {"NumDivide", 0x6F},
    {"NumLock", 0x90},   {"ScrollLock", 0x91},
    {";", 0xBA}, {"=", 0xBB}, {",", 0xBC}, {"-", 0xBD}, {".", 0xBE}, {"/", 0xBF},
    {"`", 0xC0}, {"[", 0xDB}, {"\\", 0xDC}, {"]", 0xDD}, {"'", 0xDE},
    {"Tilde", 0xC0},
};

/// Any other key, as its code: "0x5B". The settings window accepts whatever
/// key is pressed, so every code needs a spelling that parses back -- once,
/// Numpad 0 was written as "0x96", which nothing could read, and the binding
/// was lost on the next start.
std::optional<unsigned> keyFromCode(std::string_view lower) {
    if (lower.size() < 3 || lower.size() > 4 || lower.substr(0, 2) != "0x") {
        return std::nullopt;
    }
    unsigned value = 0;
    for (const char c : lower.substr(2)) {
        const bool digit = c >= '0' && c <= '9';
        const bool letter = c >= 'a' && c <= 'f';
        if (!digit && !letter) {
            return std::nullopt;
        }
        value = value * 16 + static_cast<unsigned>(digit ? c - '0' : c - 'a' + 10);
    }
    if (value == 0 || value > 0xFE) {
        return std::nullopt;
    }
    return value;
}

std::optional<unsigned> keyFromName(std::string_view name) {
    const std::string lower = toLower(name);
    if (lower.empty()) {
        return std::nullopt;
    }

    // Single letter or digit maps to its ASCII uppercase, as Win32 does.
    if (lower.size() == 1) {
        const unsigned char c = static_cast<unsigned char>(lower[0]);
        if (std::isalnum(c)) {
            return static_cast<unsigned>(std::toupper(c));
        }
    }

    // F1..F24. At most two digits, so a long run cannot overflow stoi.
    if (lower.size() >= 2 && lower.size() <= 3 && lower[0] == 'f') {
        const std::string_view digits(lower.data() + 1, lower.size() - 1);
        if (std::all_of(digits.begin(), digits.end(),
                        [](char c) { return c >= '0' && c <= '9'; })) {
            const int number = std::stoi(std::string(digits));
            if (number >= 1 && number <= 24) {
                return 0x70u + static_cast<unsigned>(number - 1);  // VK_F1
            }
        }
    }

    for (const auto& entry : kNamedKeys) {
        if (toLower(entry.name) == lower) {
            return entry.vk;
        }
    }
    return keyFromCode(lower);
}

std::optional<unsigned> modifierFromName(std::string_view name) {
    const std::string lower = toLower(name);
    if (lower == "ctrl" || lower == "control") return kControl;
    if (lower == "alt") return kAlt;
    if (lower == "shift") return kShift;
    if (lower == "win" || lower == "super" || lower == "meta" || lower == "cmd") {
        return kWin;
    }
    return std::nullopt;
}

std::string keyName(unsigned vk) {
    if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z')) {
        return std::string(1, static_cast<char>(vk));
    }
    if (vk >= 0x70 && vk <= 0x87) {
        return "F" + std::to_string(vk - 0x70 + 1);
    }
    for (const auto& entry : kNamedKeys) {
        if (entry.vk == vk && entry.name.size() > 1) {
            return std::string(entry.name);
        }
    }
    for (const auto& entry : kNamedKeys) {
        if (entry.vk == vk) {
            return std::string(entry.name);
        }
    }
    char code[8];
    std::snprintf(code, sizeof(code), "0x%02X", vk & 0xFFu);
    return code;
}

}  // namespace

std::optional<Hotkey> parseHotkey(std::string_view text) {
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t plus = text.find('+', start);
        if (plus == std::string_view::npos) {
            parts.push_back(text.substr(start));
            break;
        }
        // A literal '+' as the bound key, e.g. "Ctrl++".
        if (plus == start && start + 1 < text.size()) {
            parts.push_back(text.substr(start, 1));
            start = plus + 1;
            continue;
        }
        parts.push_back(text.substr(start, plus - start));
        start = plus + 1;
    }

    Hotkey hotkey;
    bool haveKey = false;
    for (const auto& rawPart : parts) {
        const std::string_view part = trim(rawPart);
        if (part.empty()) {
            continue;
        }
        if (const auto modifier = modifierFromName(part)) {
            hotkey.modifiers |= *modifier;
            continue;
        }
        if (haveKey) {
            return std::nullopt;  // Two non-modifier keys.
        }
        const auto key = keyFromName(part);
        if (!key) {
            return std::nullopt;
        }
        hotkey.virtualKey = *key;
        haveKey = true;
    }

    return haveKey ? std::optional<Hotkey>(hotkey) : std::nullopt;
}

std::string formatHotkey(const Hotkey& hotkey) {
    if (!hotkey.valid()) {
        return {};
    }
    std::string out;
    // Fixed order, so formatting is canonical and comparable as text.
    if (hotkey.modifiers & kControl) out += "Ctrl+";
    if (hotkey.modifiers & kAlt) out += "Alt+";
    if (hotkey.modifiers & kShift) out += "Shift+";
    if (hotkey.modifiers & kWin) out += "Win+";
    out += keyName(hotkey.virtualKey);
    return out;
}

Hotkey defaultToggleHotkey() {
    // F5: one key, reachable without letting go of movement, and not bound by
    // Escape from Tarkov. Note it is claimed globally while the overlay runs,
    // so it stops reaching other applications -- a browser will not reload on
    // F5 until the overlay exits. --toggle-key overrides it.
    return Hotkey{kNone, 0x74};  // VK_F5
}

bool isReservedCombination(const Hotkey& hotkey) {
    if (!hotkey.valid()) {
        return true;
    }
    // Ctrl+Alt+Del and Win+L never reach an application.
    constexpr unsigned kCtrlAlt = kControl | kAlt;
    if ((hotkey.modifiers & kCtrlAlt) == kCtrlAlt && hotkey.virtualKey == 0x2E) {
        return true;
    }
    if ((hotkey.modifiers & kWin) && hotkey.virtualKey == 'L') {
        return true;
    }
    // Alt+F4 and Alt+Tab would bind away something the user needs mid-raid.
    if ((hotkey.modifiers & kAlt) && hotkey.virtualKey == 0x73) {  // VK_F4
        return true;
    }
    if ((hotkey.modifiers & kAlt) && hotkey.virtualKey == 0x09) {  // VK_TAB
        return true;
    }
    return false;
}

}  // namespace tarkov::input
