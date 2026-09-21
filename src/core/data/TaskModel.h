#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/geo/Vec.h"

namespace tarkov::data {

/// A place an objective is done, in the same world coordinates as everything
/// else in this codebase.
struct TaskZone {
    std::string id;       ///< e.g. "Sandbox_1_MedicalArea_exploration"
    std::string mapId;    ///< BSG map id, as the API gives it.
    geo::Vec3 position;

    /// Elevation band. Lets a marker be placed on the floor it belongs to
    /// rather than on every floor of a building at once.
    std::optional<double> top;
    std::optional<double> bottom;

    /// True when `elevation` falls inside this zone's band, or when the zone
    /// declares no band at all.
    bool coversElevation(double elevation) const;
};

/// A place a quest item can spawn. The game picks one per raid, so all of them
/// are worth showing.
struct QuestItemLocation {
    std::string mapId;
    std::vector<geo::Vec3> positions;
};

/// One objective, reduced to what the overlay can draw or say.
struct TaskObjective {
    std::string id;
    std::string type;         ///< "visit", "mark", "plantItem", ...
    std::string description;  ///< Resolved through the locale; may be empty.
    bool optional = false;
    int count = 0;

    std::vector<std::string> mapIds;
    std::vector<TaskZone> zones;
    std::vector<QuestItemLocation> questItemLocations;

    /// Key ids needed for this objective. The API nests these as alternatives;
    /// they are flattened, so the UI must say "any of" rather than "all".
    std::vector<std::string> requiredKeyIds;

    /// True when this objective puts anything on a map.
    bool hasPlaces() const { return !zones.empty() || !questItemLocations.empty(); }
};

/// A quest.
struct Task {
    std::string id;
    std::string name;            ///< Resolved through the locale.
    std::string normalizedName;  ///< Always present and readable, e.g. "first-in-line".
    std::string traderId;
    std::string mapId;           ///< The task's own map, when it declares one.
    std::string wikiLink;

    int minPlayerLevel = 0;
    bool kappaRequired = false;
    bool lightkeeperRequired = false;

    std::vector<TaskObjective> objectives;

    /// A name fit to show: the localized one, or normalizedName turned back
    /// into words when the locale has nothing.
    std::string displayName() const;
};

/// Everything parsed out of one `/{gameMode}/tasks` response.
struct TaskSet {
    std::vector<Task> tasks;

    /// Tasks with at least one place on the given map, by BSG map id.
    std::vector<const Task*> onMap(std::string_view mapId) const;
};

/// Locale table: the flat `{"data": {key: text}}` document the API serves at
/// `/{gameMode}/tasks_{lang}`.
///
/// Values are sometimes empty strings for entries the translators have not
/// filled in, which is not an error and must not be treated as one.
class TaskLocale {
public:
    static TaskLocale parse(std::string_view json);

    /// The translation, or an empty view when the key is absent or blank.
    std::string_view lookup(std::string_view key) const;

    std::size_t size() const { return entries_.size(); }

private:
    std::vector<std::pair<std::string, std::string>> entries_;  ///< sorted by key
};

/// Parses a `/{gameMode}/tasks` response, resolving text through `locale`.
///
/// The response carries translation *keys* in the name and description fields,
/// not text: a task's name arrives as "<id> name". Without a locale the parse
/// still succeeds and names fall back to normalizedName.
///
/// Returns an empty set on malformed input rather than throwing. Bad data
/// should cost the overlay its quest layer, never take it down mid-raid.
TaskSet parseTasks(std::string_view json, const TaskLocale& locale);

/// Turns "health-care-privacy-part-2" into "Health Care Privacy Part 2".
std::string prettifyNormalizedName(std::string_view normalized);

}  // namespace tarkov::data
