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

/// A hidden stash: a container buried in the ground, found by looking for it.
enum class CacheKind {
    BuriedBarrel,  ///< "buried-barrel-cache", a blue barrel under the grass.
    Ground,        ///< "ground-cache", a wooden box under the grass.
};

/// One possible place for a hidden stash.
struct MapCache {
    geo::Vec3 position;
    CacheKind kind = CacheKind::BuriedBarrel;
    std::string name;  ///< The container's name, resolved through the locale.
};

/// The static features of one map worth drawing.
struct MapFeatures {
    std::string mapId;
    std::vector<MapExtract> extracts;
    std::vector<MapSpawn> spawns;
    std::vector<MapTransit> transits;
    std::vector<MapCache> caches;
};

/// Every map's features, keyed by BSG map id.
using MapFeatureSet = std::map<std::string, MapFeatures, std::less<>>;

/// Parses a `/{gameMode}/maps` response.
///
/// Extracts, spawns, transits and hidden stashes are read. The document's loot
/// positions are most of its 8.5 MB; of the containers among them only the two
/// kinds of stash are kept -- 318 places in the data of 2026-10-21, against
/// 7098 containers in all. Names arrive as translation keys, resolved through
/// `locale` from `/{gameMode}/maps_{lang}`, the same scheme as tasks.
///
/// Keyed by BSG map id. Malformed input yields an empty map.
MapFeatureSet parseMapFeatures(std::string_view json, const TaskLocale& locale);

/// Spawns worth showing: where *players* start, and where bosses can appear.
///
/// Bot-only spawn points outnumber these several times over -- 155 of Customs'
/// 278 are scav -- and marking them all buries the ones that matter.
std::vector<const MapSpawn*> notableSpawns(const MapFeatures& features);

}  // namespace tarkov::data
