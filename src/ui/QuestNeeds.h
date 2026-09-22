#pragma once

#include <string>
#include <vector>

#include "core/data/TaskModel.h"

namespace tarkov::ui {

/// What to bring to an objective's place, one line each: the item to plant,
/// mark with or use, and the key that opens the way.
///
/// Only for objectives that consume or use something there. For locating,
/// picking up or killing there is nothing to bring, and what to pick up is
/// already in the objective's own description.
std::vector<std::string> objectiveBringLines(const data::TaskObjective& objective,
                                             const data::TaskSet& tasks);

/// What the quest wants handed over to its trader, one line per objective.
/// Shown at every place of the quest: gathering it is part of the errand.
std::vector<std::string> handInLines(const data::Task& task, const data::TaskSet& tasks);

}  // namespace tarkov::ui
