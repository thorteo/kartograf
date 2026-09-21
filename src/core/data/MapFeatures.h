#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/data/TaskModel.h"
#include "core/geo/Vec.h"

namespace tarkov::data {

enum class ExtractFaction { Pmc, Scav, Shared };

struct MapExtract {
    std::string name;  ///< Resolved through the locale.
    ExtractFaction faction = ExtractFaction::Shared;
    geo::Vec3 position;
    std::optional<double> top;
    std::optional<double> bottom;
};

/// A place players or bots can appear.
struct MapSpawn {
    geo::Vec3 position;
    std::string zoneName;
    bool pmc = false;
    bool scav = false;
    bool player = false;  ///< Players spawn here, not only bots.
    bool boss = false;
};

/// A transit point to another location.
struct MapTransit {
    geo::Vec3 position;
    std::string description;  ///< Resolved through the locale; may be empty.
    std::string targetMapId;
};

/// The static features of one map worth drawing.
struct MapFeatures {
    std::string mapId;
    std::vector<MapExtract> extracts;
    std::vector<MapSpawn> spawns;
    std::vector<MapTransit> transits;
};

/// Parses a `/{gameMode}/maps` response.
///
/// Only extracts, spawns and transits are read. The document also carries loot
/// positions, which are most of its 8.5 MB and not something this overlay
/// draws. Names arrive as translation keys, resolved through `locale` from
/// `/{gameMode}/maps_{lang}`, the same scheme as tasks.
///
/// Keyed by BSG map id. Malformed input yields an empty map.
std::map<std::string, MapFeatures, std::less<>> parseMapFeatures(
    std::string_view json, const TaskLocale& locale);

/// Spawns worth showing: where *players* start, and where bosses can appear.
///
/// Bot-only spawn points outnumber these several times over -- 155 of Customs'
/// 278 are scav -- and marking them all buries the ones that matter.
std::vector<const MapSpawn*> notableSpawns(const MapFeatures& features);

}  // namespace tarkov::data
