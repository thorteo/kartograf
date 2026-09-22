#include "core/data/TaskModel.h"

#include <algorithm>
#include <cctype>

#include <nlohmann/json.hpp>

namespace tarkov::data {
namespace {

using nlohmann::json;

const json* find(const json& object, const char* key) {
    if (!object.is_object()) {
        return nullptr;
    }
    const auto it = object.find(key);
    return it == object.end() || it->is_null() ? nullptr : &*it;
}

std::string stringAt(const json& object, const char* key) {
    const json* value = find(object, key);
    return value != nullptr && value->is_string() ? value->get<std::string>()
                                                  : std::string{};
}

int intAt(const json& object, const char* key, int fallback = 0) {
    const json* value = find(object, key);
    return value != nullptr && value->is_number() ? value->get<int>() : fallback;
}

bool boolAt(const json& object, const char* key) {
    const json* value = find(object, key);
    return value != nullptr && value->is_boolean() && value->get<bool>();
}

std::optional<double> doubleAt(const json& object, const char* key) {
    const json* value = find(object, key);
    if (value == nullptr || !value->is_number()) {
        return std::nullopt;
    }
    return value->get<double>();
}

std::optional<geo::Vec3> readPosition(const json& object) {
    if (!object.is_object()) {
        return std::nullopt;
    }
    const auto x = doubleAt(object, "x");
    const auto y = doubleAt(object, "y");
    const auto z = doubleAt(object, "z");
    if (!x || !y || !z) {
        return std::nullopt;
    }
    return geo::Vec3{*x, *y, *z};
}

/// Collects key ids out of the API's list-of-alternatives shape.
void readRequiredKeys(const json& objective, std::vector<std::string>& out) {
    const json* required = find(objective, "requiredKeys");
    if (required == nullptr || !required->is_array()) {
        return;
    }
    for (const auto& alternatives : *required) {
        if (alternatives.is_array()) {
            for (const auto& key : alternatives) {
                if (key.is_string()) {
                    out.push_back(key.get<std::string>());
                }
            }
        } else if (alternatives.is_string()) {
            out.push_back(alternatives.get<std::string>());
        }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
}

/// An item reference: the API writes either a bare id or an object with one.
std::string itemIdOf(const json& value) {
    if (value.is_string()) {
        return value.get<std::string>();
    }
    return stringAt(value, "id");
}

/// Collects the items an objective is about, whichever field its type uses.
void readItems(const json& objective, std::vector<std::string>& out) {
    for (const char* key : {"items", "useAny"}) {
        if (const json* list = find(objective, key); list != nullptr && list->is_array()) {
            for (const auto& item : *list) {
                if (auto id = itemIdOf(item); !id.empty()) {
                    out.push_back(std::move(id));
                }
            }
        }
    }
    for (const char* key : {"markerItem", "questItem"}) {
        if (const json* item = find(objective, key); item != nullptr) {
            if (auto id = itemIdOf(*item); !id.empty()) {
                out.push_back(std::move(id));
            }
        }
    }
    // Deduplicated, but in the API's order: the first alternatives listed are
    // the common ones, and a long list is shown cut short.
    std::vector<std::string> unique;
    for (auto& id : out) {
        if (std::find(unique.begin(), unique.end(), id) == unique.end()) {
            unique.push_back(std::move(id));
        }
    }
    out = std::move(unique);
}

void readZones(const json& objective, std::vector<TaskZone>& out) {
    const json* zones = find(objective, "zones");
    if (zones == nullptr || !zones->is_array()) {
        return;
    }
    for (const auto& entry : *zones) {
        const json* position = find(entry, "position");
        if (position == nullptr) {
            continue;
        }
        const auto world = readPosition(*position);
        if (!world) {
            continue;
        }
        TaskZone zone;
        zone.id = stringAt(entry, "id");
        zone.mapId = stringAt(entry, "map");
        zone.position = *world;
        zone.top = doubleAt(entry, "top");
        zone.bottom = doubleAt(entry, "bottom");
        out.push_back(std::move(zone));
    }
}

void readQuestItemLocations(const json& objective,
                            std::vector<QuestItemLocation>& out) {
    const json* locations = find(objective, "possibleLocations");
    if (locations == nullptr || !locations->is_array()) {
        return;
    }
    for (const auto& entry : *locations) {
        QuestItemLocation location;
        location.mapId = stringAt(entry, "map");

        const json* positions = find(entry, "positions");
        if (positions != nullptr && positions->is_array()) {
            for (const auto& position : *positions) {
                if (const auto world = readPosition(position)) {
                    location.positions.push_back(*world);
                }
            }
        }
        if (!location.positions.empty()) {
            out.push_back(std::move(location));
        }
    }
}

}  // namespace

std::string prettifyNormalizedName(std::string_view normalized) {
    std::string out;
    out.reserve(normalized.size());
    bool startOfWord = true;
    for (const char c : normalized) {
        if (c == '-' || c == '_') {
            out.push_back(' ');
            startOfWord = true;
            continue;
        }
        out.push_back(startOfWord
                          ? static_cast<char>(std::toupper(
                                static_cast<unsigned char>(c)))
                          : c);
        startOfWord = false;
    }
    return out;
}

std::string Task::displayName() const {
    if (!name.empty()) {
        return name;
    }
    // The locale can be missing entirely, or carry a blank entry for a task the
    // translators have not reached. normalizedName is always present.
    return prettifyNormalizedName(normalizedName);
}

std::vector<const Task*> TaskSet::onMap(std::string_view mapId) const {
    std::vector<const Task*> found;
    for (const auto& task : tasks) {
        const bool relevant = std::any_of(
            task.objectives.begin(), task.objectives.end(),
            [&](const TaskObjective& objective) {
                return std::any_of(objective.zones.begin(), objective.zones.end(),
                                   [&](const TaskZone& zone) {
                                       return zone.mapId == mapId;
                                   }) ||
                       std::any_of(objective.questItemLocations.begin(),
                                   objective.questItemLocations.end(),
                                   [&](const QuestItemLocation& location) {
                                       return location.mapId == mapId;
                                   });
            });
        if (relevant) {
            found.push_back(&task);
        }
    }
    std::sort(found.begin(), found.end(), [](const Task* a, const Task* b) {
        return a->displayName() < b->displayName();
    });
    return found;
}

TaskLocale TaskLocale::parse(std::string_view text) {
    TaskLocale locale;
    const json root = json::parse(text, nullptr, /*allow_exceptions=*/false);
    const json* data = find(root, "data");
    if (data == nullptr || !data->is_object()) {
        return locale;
    }
    locale.entries_.reserve(data->size());
    for (const auto& [key, value] : data->items()) {
        if (value.is_string()) {
            locale.entries_.emplace_back(key, value.get<std::string>());
        }
    }
    std::sort(locale.entries_.begin(), locale.entries_.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    return locale;
}

std::string_view TaskLocale::lookup(std::string_view key) const {
    const auto it = std::lower_bound(
        entries_.begin(), entries_.end(), key,
        [](const auto& entry, std::string_view wanted) {
            return entry.first < wanted;
        });
    if (it == entries_.end() || it->first != key) {
        return {};
    }
    return it->second;
}

TaskSet parseTasks(std::string_view text, const TaskLocale& locale,
                   const TaskLocale& itemLocale) {
    TaskSet set;
    const json root = json::parse(text, nullptr, /*allow_exceptions=*/false);
    const json* data = find(root, "data");
    if (data == nullptr) {
        return set;
    }
    const json* tasks = find(*data, "tasks");
    if (tasks == nullptr || !tasks->is_object()) {
        return set;
    }

    set.tasks.reserve(tasks->size());
    for (const auto& [id, entry] : tasks->items()) {
        if (!entry.is_object()) {
            continue;
        }
        Task task;
        task.id = id;
        task.normalizedName = stringAt(entry, "normalizedName");
        // The name field holds a translation key, not text.
        task.name = std::string(locale.lookup(stringAt(entry, "name")));
        task.traderId = stringAt(entry, "trader");
        task.mapId = stringAt(entry, "map");
        task.wikiLink = stringAt(entry, "wikiLink");
        task.minPlayerLevel = intAt(entry, "minPlayerLevel");
        task.kappaRequired = boolAt(entry, "kappaRequired");
        task.lightkeeperRequired = boolAt(entry, "lightkeeperRequired");

        if (const json* requirements = find(entry, "taskRequirements");
            requirements != nullptr && requirements->is_array()) {
            for (const auto& requirement : *requirements) {
                const std::string prerequisite = stringAt(requirement, "task");
                // The statuses are alternatives: ["active", "complete"] is
                // satisfied by a quest still under way, ["complete", "failed"]
                // by one that was failed. Only a requirement that accepts
                // nothing but completion proves the prerequisite was done.
                const json* status = find(requirement, "status");
                bool needsCompletion = false;
                if (status != nullptr && status->is_array() && !status->empty()) {
                    needsCompletion = std::all_of(
                        status->begin(), status->end(), [](const json& value) {
                            return value.is_string() && value == "complete";
                        });
                }
                if (!prerequisite.empty() && needsCompletion) {
                    task.prerequisiteIds.push_back(prerequisite);
                }
            }
        }

        const json* objectives = find(entry, "objectives");
        if (objectives != nullptr && objectives->is_array()) {
            for (const auto& source : *objectives) {
                if (!source.is_object()) {
                    continue;
                }
                TaskObjective objective;
                objective.id = stringAt(source, "id");
                objective.type = stringAt(source, "type");
                objective.description =
                    std::string(locale.lookup(stringAt(source, "description")));
                objective.optional = boolAt(source, "optional");
                objective.count = intAt(source, "count");

                if (const json* maps = find(source, "maps");
                    maps != nullptr && maps->is_array()) {
                    for (const auto& mapId : *maps) {
                        if (mapId.is_string()) {
                            objective.mapIds.push_back(mapId.get<std::string>());
                        }
                    }
                }

                readZones(source, objective.zones);
                readQuestItemLocations(source, objective.questItemLocations);
                readRequiredKeys(source, objective.requiredKeyIds);
                readItems(source, objective.itemIds);
                objective.foundInRaid = boolAt(source, "foundInRaid");

                task.objectives.push_back(std::move(objective));
            }
        }
        set.tasks.push_back(std::move(task));
    }

    std::sort(set.tasks.begin(), set.tasks.end(),
              [](const Task& a, const Task& b) { return a.id < b.id; });

    // Name every item an objective mentions. Ordinary items are named by the
    // items locale; quest items only by the tasks locale.
    std::vector<std::string> ids;
    for (const auto& task : set.tasks) {
        for (const auto& objective : task.objectives) {
            ids.insert(ids.end(), objective.itemIds.begin(), objective.itemIds.end());
            ids.insert(ids.end(), objective.requiredKeyIds.begin(),
                       objective.requiredKeyIds.end());
        }
    }
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    for (const auto& id : ids) {
        const std::string key = id + " Name";
        std::string_view name = itemLocale.lookup(key);
        if (name.empty()) {
            name = locale.lookup(key);
        }
        if (!name.empty()) {
            set.itemNames.emplace_back(id, std::string(name));
        }
    }
    return set;
}

std::string_view TaskSet::itemName(std::string_view itemId) const {
    const auto it = std::lower_bound(
        itemNames.begin(), itemNames.end(), itemId,
        [](const auto& entry, std::string_view id) { return entry.first < id; });
    return it != itemNames.end() && it->first == itemId ? std::string_view(it->second)
                                                        : std::string_view{};
}

ObjectiveKind objectiveKind(std::string_view type) {
    if (type == "visit") return ObjectiveKind::Visit;
    if (type == "mark") return ObjectiveKind::Mark;
    if (type == "plantItem" || type == "plantQuestItem") return ObjectiveKind::Plant;
    if (type == "findQuestItem" || type == "findItem") return ObjectiveKind::Find;
    if (type == "shoot") return ObjectiveKind::Shoot;
    if (type == "useItem") return ObjectiveKind::Use;
    return ObjectiveKind::Other;
}

}  // namespace tarkov::data
