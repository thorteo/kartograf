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

    /// The zone's trigger volume, vertically. Not its floor: a couple of
    /// metres tall; see research/tarkov-dev-rest-api.md.
    std::optional<double> top;
    std::optional<double> bottom;
};

/// A place a quest item can spawn. The game picks one per raid, so all of them
/// are worth showing.
struct QuestItemLocation {
    std::string mapId;
    std::vector<geo::Vec3> positions;
};

/// What an objective asks the player to do at a place, for choosing its icon.
///
/// Only the types that put something on a map are told apart. Counted in
/// tarkov.dev's task data on 2026-09-21: visit 197 objectives with places,
/// plantItem 122, findQuestItem 110, mark 83, shoot 22, plantQuestItem 13,
/// useItem 9. Every other type has none.
enum class ObjectiveKind {
    /// visit: reach a place. Nearly always worded "locate": 138 of the 197
    /// begin "Найти", about 30 more "Проверить", "Осмотреть", "Обнаружить";
    /// 9 say "Посетить".
    Visit,
    Mark,   ///< mark: set an MS2000 marker.
    Plant,  ///< plantItem, plantQuestItem: leave an item.
    Find,   ///< findQuestItem, findItem: pick an item up.
    Shoot,  ///< shoot: kill, within a zone.
    Use,    ///< useItem: use an item at a place.
    Other,  ///< Anything else, including types added later.
};

ObjectiveKind objectiveKind(std::string_view type);

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

    /// Items the objective is about: what to plant, hand over, find or use,
    /// or the marker to set. Alternatives -- any one of them will do, `count`
    /// times over. Gathered from the API's `items`, `useAny`, `markerItem` and
    /// `questItem`, whichever the type carries.
    std::vector<std::string> itemIds;
    /// The items must have been found in raid.
    bool foundInRaid = false;

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

    /// Quests that must be *completed* before this one can be taken.
    ///
    /// The API lists each requirement's acceptable statuses as alternatives:
    /// "complete", but also "active" and "failed", and combinations -- in the
    /// data of 2026-09-22, 210 plain "complete", 10 "active or complete", 4
    /// "complete or failed". Only the plain ones are kept, because they are
    /// what lets progress be inferred: if a quest has been started, everything
    /// it required is necessarily done. Chemical part 3 accepts part 2 still
    /// active, and inferring it done hid part 2's objectives from the map.
    std::vector<std::string> prerequisiteIds;

    /// A name fit to show: the localized one, or normalizedName turned back
    /// into words when the locale has nothing.
    std::string displayName() const;
};

/// Everything parsed out of one `/{gameMode}/tasks` response.
struct TaskSet {
    std::vector<Task> tasks;

    /// Display names of the items the objectives mention, by item id. Only
    /// those, not the whole item catalogue: a few hundred names rather than
    /// six thousand.
    std::vector<std::pair<std::string, std::string>> itemNames;  ///< sorted by id

    /// The item's name, or an empty view when no locale named it.
    std::string_view itemName(std::string_view itemId) const;

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
/// Item names come from `itemLocale`, the `/{gameMode}/items_{lang}` document,
/// and for quest items from `locale` itself, which carries them.
///
/// The response carries translation *keys* in the name and description fields,
/// not text: a task's name arrives as "<id> name". Without a locale the parse
/// still succeeds and names fall back to normalizedName.
///
/// Returns an empty set on malformed input rather than throwing. Bad data
/// should cost the overlay its quest layer, never take it down mid-raid.
TaskSet parseTasks(std::string_view json, const TaskLocale& locale,
                   const TaskLocale& itemLocale = TaskLocale{});

/// Turns "health-care-privacy-part-2" into "Health Care Privacy Part 2".
std::string prettifyNormalizedName(std::string_view normalized);

}  // namespace tarkov::data
