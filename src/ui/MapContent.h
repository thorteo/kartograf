#pragma once

#include <string_view>
#include <vector>

#include "core/data/MapFeatures.h"
#include "core/data/MapIds.h"
#include "core/data/QuestProgress.h"
#include "core/data/TaskModel.h"
#include "ui/MapView.h"
#include "ui/QuestList.h"

namespace tarkov::ui {

/// What goes on a map from the data rather than from the raid. Built when the
/// map, the data or the progress changes, never per frame: walking through a
/// raid must not re-scan 515 tasks every frame.
///
/// A map is named as it is drawn (`drawnAsName`, e.g. "factory"); every BSG id
/// that draws as it is included. Night Factory and Factory share a picture,
/// and a quest on either belongs on it.

/// Extracts, notable spawns and transits, replacing whatever `out` held.
void buildMapPoints(const data::MapFeatureSet& features, const data::MapIdTable& mapIds,
                    std::string_view drawnAsName, MapOverlays& out);

/// Quest markers, one per zone and quest item spawn, unmerged. With
/// `activeOnly`, only the player's current quests: every quest in the game on
/// one map is 150+ markers -- noise, not help.
std::vector<QuestPlace> buildQuestPlaces(const data::TaskSet& tasks,
                                         const data::MapIdTable& mapIds,
                                         std::string_view drawnAsName,
                                         const data::EffectiveProgress* activeOnly);

/// Every quest with a place on the map, whatever its state, so it can be
/// corrected from the list.
std::vector<QuestListEntry> buildQuestList(const data::TaskSet& tasks,
                                           const data::MapIdTable& mapIds,
                                           std::string_view drawnAsName,
                                           const data::EffectiveProgress& effective,
                                           const data::QuestProgress& recorded);

}  // namespace tarkov::ui
