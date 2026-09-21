#include "core/input/Hotkey.h"

#include <algorithm>
#include <array>
#include <cctype>
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
constexpr std::array<NamedKey, 34> kNamedKeys{{
    {"backspace", 0x08}, {"tab", 0x09},    {"enter", 0x0D},  {"return", 0x0D},
    {"pause", 0x13},     {"capslock", 0x14}, {"esc", 0x1B},  {"escape", 0x1B},
    {"space", 0x20},     {"pageup", 0x21}, {"pagedown", 0x22}, {"end", 0x23},
    {"home", 0x24},      {"left", 0x25},   {"up", 0x26},     {"right", 0x27},
    {"down", 0x28},      {"insert", 0x2D}, {"delete", 0x2E}, {"del", 0x2E},
    {"numlock", 0x90},   {"scrolllock", 0x91},
    {";", 0xBA}, {"=", 0xBB}, {",", 0xBC}, {"-", 0xBD}, {".", 0xBE}, {"/", 0xBF},
    {"`", 0xC0}, {"[", 0xDB}, {"\\", 0xDC}, {"]", 0xDD}, {"'", 0xDE},
    {"tilde", 0xC0},
}};

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

    // F1..F24
    if (lower.size() >= 2 && lower[0] == 'f') {
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
        if (entry.name == lower) {
            return entry.vk;
        }
    }
    return std::nullopt;
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
            std::string name(entry.name);
            name[0] = static_cast<char>(std::toupper(
                static_cast<unsigned char>(name[0])));
            return name;
        }
    }
    for (const auto& entry : kNamedKeys) {
        if (entry.vk == vk) {
            return std::string(entry.name);
        }
    }
    return "0x" + std::to_string(vk);
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
    return Hotkey{kAlt, 'M'};
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
