#include "ui/MapContent.h"

#include <algorithm>

#include "ui/QuestNeeds.h"
#include "ui/Strings.h"

namespace tarkov::ui {
namespace {

/// Whether a BSG map id is one of `ids`.
bool among(const std::vector<std::string_view>& ids, std::string_view mapId) {
    return std::find(ids.begin(), ids.end(), mapId) != ids.end();
}

MarkerIcon extractIcon(data::ExtractFaction faction) {
    switch (faction) {
        case data::ExtractFaction::Pmc: return MarkerIcon::ExtractPmc;
        case data::ExtractFaction::Scav: return MarkerIcon::ExtractScav;
        case data::ExtractFaction::Shared: break;
    }
    return MarkerIcon::ExtractShared;
}

}  // namespace

void buildMapPoints(const data::MapFeatureSet& features, const data::MapIdTable& mapIds,
                    std::string_view drawnAsName, MapOverlays& out) {
    out.extracts.clear();
    out.spawns.clear();
    out.transits.clear();
    for (const auto id : mapIds.idsFor(drawnAsName)) {
        const auto it = features.find(id);
        if (it == features.end()) {
            continue;
        }
        for (const auto& extract : it->second.extracts) {
            MapPoint point;
            point.world = extract.position;
            point.label = extract.name;
            point.icon = extractIcon(extract.faction);
            out.extracts.push_back(std::move(point));
        }
        for (const auto* spawn : data::notableSpawns(it->second)) {
            MapPoint point;
            point.world = spawn->position;
            point.label = spawn->boss ? tr(Str::BossSpawn) : tr(Str::PmcSpawn);
            point.icon = spawn->boss ? MarkerIcon::SpawnBoss : MarkerIcon::SpawnPmc;
            out.spawns.push_back(std::move(point));
        }
        for (const auto& transit : it->second.transits) {
            MapPoint point;
            point.world = transit.position;
            if (transit.description.empty()) {
                const auto target = mapIds.drawnAs(transit.targetMapId);
                point.label =
                    trf(Str::TransitTo, target ? mapDisplayName(*target).c_str() : "?");
            } else {
                point.label = transit.description;
            }
            point.icon = MarkerIcon::Transit;
            out.transits.push_back(std::move(point));
        }
    }
}

std::vector<QuestPlace> buildQuestPlaces(const data::TaskSet& tasks,
                                         const data::MapIdTable& mapIds,
                                         std::string_view drawnAsName,
                                         const data::EffectiveProgress* activeOnly) {
    std::vector<QuestPlace> places;
    const auto ids = mapIds.idsFor(drawnAsName);
    if (ids.empty()) {
        return places;
    }

    for (const auto& task : tasks.tasks) {
        if (activeOnly != nullptr &&
            activeOnly->stateOf(task.id) != data::QuestState::Active) {
            continue;
        }
        const std::string name = task.displayName();
        // What the quest wants handed over, shown at every one of its places.
        const auto handIn = handInLines(task, tasks);
        for (const auto& objective : task.objectives) {
            auto needs = objectiveBringLines(objective, tasks);
            needs.insert(needs.end(), handIn.begin(), handIn.end());
            const auto placeFor = [&](const geo::Vec3& world) {
                QuestPlace place;
                place.world = world;
                place.taskName = name;
                place.detail = objective.description;
                place.optional = objective.optional;
                place.kind = data::objectiveKind(objective.type);
                place.needs = needs;
                return place;
            };
            for (const auto& zone : objective.zones) {
                if (among(ids, zone.mapId)) {
                    QuestPlace place = placeFor(zone.position);
                    place.top = zone.top;
                    place.bottom = zone.bottom;
                    places.push_back(std::move(place));
                }
            }
            for (const auto& location : objective.questItemLocations) {
                if (!among(ids, location.mapId)) {
                    continue;
                }
                for (const auto& position : location.positions) {
                    QuestPlace place = placeFor(position);
                    place.isQuestItem = true;
                    places.push_back(std::move(place));
                }
            }
        }
    }
    return places;
}

std::vector<QuestListEntry> buildQuestList(const data::TaskSet& tasks,
                                           const data::MapIdTable& mapIds,
                                           std::string_view drawnAsName,
                                           const data::EffectiveProgress& effective,
                                           const data::QuestProgress& recorded) {
    std::vector<QuestListEntry> entries;
    const auto ids = mapIds.idsFor(drawnAsName);
    for (const auto& task : tasks.tasks) {
        int here = 0;
        for (const auto& objective : task.objectives) {
            for (const auto& zone : objective.zones) {
                here += among(ids, zone.mapId);
            }
            for (const auto& location : objective.questItemLocations) {
                here += among(ids, location.mapId);
            }
        }
        if (here == 0) {
            continue;
        }
        QuestListEntry entry;
        entry.id = task.id;
        entry.name = task.displayName();
        entry.objectivesHere = here;
        entry.state = effective.stateOf(task.id);
        entry.inferred = effective.inferredCompleted.count(task.id) != 0;
        const auto* record = recorded.find(task.id);
        entry.manual = record != nullptr && record->manual;
        entries.push_back(std::move(entry));
    }
    return entries;
}

}  // namespace tarkov::ui
