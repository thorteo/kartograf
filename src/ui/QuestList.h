#pragma once

#include <functional>
#include <string>
#include <vector>

#include "core/data/QuestProgress.h"

namespace tarkov::ui {

/// One quest with something to do on the current map.
struct QuestListEntry {
    std::string id;
    std::string name;
    int objectivesHere = 0;
    data::QuestState state = data::QuestState::Unknown;
    /// Completed only because a later quest proves it, never seen directly.
    bool inferred = false;
    bool manual = false;
};

struct QuestListActions {
    /// Record a correction. Called on the UI thread.
    std::function<void(const std::string& questId, data::QuestState state)> setState;
    /// Drop a correction, back to whatever the logs say.
    std::function<void(const std::string& questId)> clearCorrection;
};

/// The quests on this map, with the player's controls for correcting them.
///
/// Only drawn while the overlay takes input: it is a panel you open to fix
/// something, not something to read mid-fight.
///
/// Returns true when the user changed anything.
bool drawQuestList(const std::vector<QuestListEntry>& entries, bool filteredToActive,
                   int activeTotal, const QuestListActions& actions);

}  // namespace tarkov::ui
