#include "core/data/MapFeatures.h"

#include <nlohmann/json.hpp>

namespace tarkov::data {
namespace {

using nlohmann::json;

const json* child(const json& object, const char* key) {
    if (!object.is_object()) {
        return nullptr;
    }
    const auto it = object.find(key);
    return it == object.end() || it->is_null() ? nullptr : &*it;
}

std::string text(const json& object, const char* key) {
    const json* value = child(object, key);
    return value != nullptr && value->is_string() ? value->get<std::string>()
                                                  : std::string{};
}

std::optional<double> number(const json& object, const char* key) {
    const json* value = child(object, key);
    if (value == nullptr || !value->is_number()) {
        return std::nullopt;
    }
    return value->get<double>();
}

std::optional<geo::Vec3> position(const json& object) {
    const json* p = child(object, "position");
    if (p == nullptr) {
        return std::nullopt;
    }
    const auto x = number(*p, "x");
    const auto y = number(*p, "y");
    const auto z = number(*p, "z");
    if (!x || !y || !z) {
        return std::nullopt;
    }
    return geo::Vec3{*x, *y, *z};
}

bool contains(const json* list, std::string_view value) {
    if (list == nullptr || !list->is_array()) {
        return false;
    }
    for (const auto& entry : *list) {
        if (entry.is_string() && entry.get<std::string>() == value) {
            return true;
        }
    }
    return false;
}

std::string translated(const TaskLocale& locale, const std::string& key) {
    const std::string_view value = locale.lookup(key);
    // Untranslated extract names are still readable English keys, so fall back
    // to the key itself rather than to nothing.
    return value.empty() ? key : std::string(value);
}

}  // namespace

MapFeatureSet parseMapFeatures(std::string_view source, const TaskLocale& locale) {
    MapFeatureSet out;
    const json root = json::parse(source, nullptr, /*allow_exceptions=*/false);
    const json* data = child(root, "data");
    const json* maps = data != nullptr ? child(*data, "maps") : nullptr;
    if (maps == nullptr || !maps->is_object()) {
        return out;
    }

    for (const auto& [id, entry] : maps->items()) {
        MapFeatures features;
        features.mapId = id;

        if (const json* extracts = child(entry, "extracts");
            extracts != nullptr && extracts->is_array()) {
            for (const auto& e : *extracts) {
                const auto where = position(e);
                if (!where) {
                    continue;
                }
                MapExtract extract;
                extract.name = translated(locale, text(e, "name"));
                const std::string faction = text(e, "faction");
                extract.faction = faction == "pmc"    ? ExtractFaction::Pmc
                                  : faction == "scav" ? ExtractFaction::Scav
                                                      : ExtractFaction::Shared;
                extract.position = *where;
                extract.top = number(e, "top");
                extract.bottom = number(e, "bottom");
                features.extracts.push_back(std::move(extract));
            }
        }

        if (const json* spawns = child(entry, "spawns");
            spawns != nullptr && spawns->is_array()) {
            for (const auto& s : *spawns) {
                const auto where = position(s);
                if (!where) {
                    continue;
                }
                MapSpawn spawn;
                spawn.position = *where;
                spawn.zoneName = text(s, "zoneName");
                const json* sides = child(s, "sides");
                const json* categories = child(s, "categories");
                spawn.pmc = contains(sides, "pmc");
                spawn.scav = contains(sides, "scav");
                spawn.player = contains(categories, "player");
                spawn.boss = contains(categories, "boss");
                features.spawns.push_back(std::move(spawn));
            }
        }

        if (const json* transits = child(entry, "transits");
            transits != nullptr && transits->is_array()) {
            for (const auto& t : *transits) {
                const auto where = position(t);
                if (!where) {
                    continue;
                }
                MapTransit transit;
                transit.position = *where;
                const std::string key = text(t, "description");
                transit.description = std::string(locale.lookup(key));
                transit.targetMapId = text(t, "map");
                features.transits.push_back(std::move(transit));
            }
        }

        out.emplace(id, std::move(features));
    }
    return out;
}

std::vector<const MapSpawn*> notableSpawns(const MapFeatures& features) {
    std::vector<const MapSpawn*> out;
    for (const auto& spawn : features.spawns) {
        if (spawn.boss || (spawn.pmc && spawn.player)) {
            out.push_back(&spawn);
        }
    }
    return out;
}

}  // namespace tarkov::data
