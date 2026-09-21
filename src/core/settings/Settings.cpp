#include "core/settings/Settings.h"

#include <algorithm>

#include <nlohmann/json.hpp>

namespace tarkov::settings {
namespace {

using nlohmann::json;

constexpr float kMinOpacity = 0.15f;
constexpr float kMaxOpacity = 1.0f;

void readHotkey(const json& root, const char* key, input::Hotkey& out,
                std::vector<Issue>* issues) {
    const auto it = root.find(key);
    if (it == root.end()) {
        return;
    }
    if (!it->is_string()) {
        if (issues) issues->push_back({key, "not a string"});
        return;
    }
    const auto parsed = input::parseHotkey(it->get<std::string>());
    if (!parsed) {
        if (issues) issues->push_back({key, "unrecognised key: " + it->get<std::string>()});
        return;
    }
    out = *parsed;
}

template <typename T>
void readValue(const json& root, const char* key, T& out, std::vector<Issue>* issues) {
    const auto it = root.find(key);
    if (it == root.end()) {
        return;
    }
    try {
        out = it->get<T>();
    } catch (const json::exception&) {
        if (issues) issues->push_back({key, "wrong type"});
    }
}

}  // namespace

std::string serialize(const Settings& s) {
    nlohmann::ordered_json root;
    root["version"] = 1;
    root["mapKey"] = input::formatHotkey(s.mapKey);
    root["interactKey"] = input::formatHotkey(s.interactKey);
    root["hudKey"] = input::formatHotkey(s.hudKey);
    root["language"] = s.language;
    root["mapOpacity"] = s.mapOpacity;
    root["preferAbstractMap"] = s.preferAbstractMap;
    root["showQuests"] = s.showQuests;
    root["showExtracts"] = s.showExtracts;
    root["showTransits"] = s.showTransits;
    root["showSpawns"] = s.showSpawns;
    root["dimOtherFloors"] = s.dimOtherFloors;
    root["showHud"] = s.showHud;
    root["excludeFromCapture"] = s.excludeFromCapture;
    root["showSettingsOnStart"] = s.showSettingsOnStart;
    return root.dump(2);
}

Settings parse(std::string_view text, std::vector<Issue>* issues) {
    Settings s;
    if (text.empty()) {
        return s;
    }
    const json root = json::parse(text, nullptr, /*allow_exceptions=*/false);
    if (!root.is_object()) {
        if (issues) issues->push_back({"file", "not a settings file; using defaults"});
        return s;
    }
    readHotkey(root, "mapKey", s.mapKey, issues);
    readHotkey(root, "interactKey", s.interactKey, issues);
    readHotkey(root, "hudKey", s.hudKey, issues);
    readValue(root, "language", s.language, issues);
    readValue(root, "mapOpacity", s.mapOpacity, issues);
    readValue(root, "preferAbstractMap", s.preferAbstractMap, issues);
    readValue(root, "showQuests", s.showQuests, issues);
    readValue(root, "showExtracts", s.showExtracts, issues);
    readValue(root, "showTransits", s.showTransits, issues);
    readValue(root, "showSpawns", s.showSpawns, issues);
    readValue(root, "dimOtherFloors", s.dimOtherFloors, issues);
    readValue(root, "showHud", s.showHud, issues);
    readValue(root, "excludeFromCapture", s.excludeFromCapture, issues);
    readValue(root, "showSettingsOnStart", s.showSettingsOnStart, issues);
    return s;
}

std::vector<Issue> sanitize(Settings& s) {
    std::vector<Issue> changed;
    const Settings defaults;

    const float opacity = std::clamp(s.mapOpacity, kMinOpacity, kMaxOpacity);
    if (opacity != s.mapOpacity || s.mapOpacity != s.mapOpacity /* NaN */) {
        s.mapOpacity = s.mapOpacity != s.mapOpacity ? defaults.mapOpacity : opacity;
        changed.push_back({"mapOpacity", "out of range"});
    }
    if (s.language != "ru" && s.language != "en") {
        s.language = defaults.language;
        changed.push_back({"language", "unknown language"});
    }

    // Each key must be bindable, and no two may be the same combination -- a
    // second RegisterHotKey on the same combination fails, and the player would
    // be left with one action that no key triggers.
    struct Slot {
        input::Hotkey* key;
        input::Hotkey fallback;
        const char* name;
    };
    Slot slots[] = {{&s.mapKey, defaults.mapKey, "mapKey"},
                    {&s.interactKey, defaults.interactKey, "interactKey"},
                    {&s.hudKey, defaults.hudKey, "hudKey"}};
    for (auto& slot : slots) {
        if (input::isReservedCombination(*slot.key)) {
            *slot.key = slot.fallback;
            changed.push_back({slot.name, "reserved combination"});
        }
    }
    for (std::size_t i = 0; i < std::size(slots); ++i) {
        for (std::size_t j = 0; j < i; ++j) {
            if (*slots[i].key == *slots[j].key) {
                *slots[i].key = slots[i].fallback;
                changed.push_back({slots[i].name, "same key as another action"});
                if (*slots[i].key == *slots[j].key) {
                    // The fallback collides too; clear rather than duplicate.
                    *slots[i].key = input::Hotkey{};
                }
            }
        }
    }
    return changed;
}

}  // namespace tarkov::settings
