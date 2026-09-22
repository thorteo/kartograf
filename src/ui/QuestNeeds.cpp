#include "ui/QuestNeeds.h"

#include "ui/Strings.h"

namespace tarkov::ui {
namespace {

/// A long list of alternatives -- one quest takes any of 52 medicines -- is
/// cut short: the first few say what kind of thing is wanted.
constexpr std::size_t kMaxListed = 4;

std::vector<std::string> namesOf(const std::vector<std::string>& ids,
                                 const data::TaskSet& tasks) {
    std::vector<std::string> names;
    for (const auto& id : ids) {
        if (const auto name = tasks.itemName(id); !name.empty()) {
            names.emplace_back(name);
        }
    }
    return names;
}

std::string listOf(const std::vector<std::string>& names) {
    std::string out;
    for (std::size_t i = 0; i < names.size() && i < kMaxListed; ++i) {
        out += i == 0 ? "" : ", ";
        out += names[i];
    }
    if (names.size() > kMaxListed) {
        out += ", ";
        out += trf(Str::NeedAndMore, static_cast<int>(names.size() - kMaxListed));
    }
    return out;
}

std::string counted(const std::string& name, int count) {
    return count > 1 ? trf(Str::NeedCount, name.c_str(), count) : name;
}

std::string withFoundInRaid(std::string line, bool foundInRaid) {
    if (foundInRaid) {
        line += " (";
        line += tr(Str::NeedFoundInRaid);
        line += ")";
    }
    return line;
}

}  // namespace

std::vector<std::string> objectiveBringLines(const data::TaskObjective& objective,
                                             const data::TaskSet& tasks) {
    std::vector<std::string> lines;
    const auto kind = data::objectiveKind(objective.type);
    if (kind == data::ObjectiveKind::Mark || kind == data::ObjectiveKind::Plant ||
        kind == data::ObjectiveKind::Use) {
        const auto names = namesOf(objective.itemIds, tasks);
        if (names.size() == 1) {
            lines.push_back(withFoundInRaid(
                trf(Str::NeedBring, counted(names[0], objective.count).c_str()),
                objective.foundInRaid));
        } else if (names.size() > 1) {
            lines.push_back(withFoundInRaid(
                trf(Str::NeedBringAnyOf, listOf(names).c_str()), objective.foundInRaid));
        }
    }
    const auto keys = namesOf(objective.requiredKeyIds, tasks);
    if (keys.size() == 1) {
        lines.push_back(trf(Str::NeedKey, keys[0].c_str()));
    } else if (keys.size() > 1) {
        lines.push_back(trf(Str::NeedKeyAnyOf, listOf(keys).c_str()));
    }
    return lines;
}

std::vector<std::string> handInLines(const data::Task& task, const data::TaskSet& tasks) {
    std::vector<std::string> lines;
    for (const auto& objective : task.objectives) {
        if (objective.type != "giveItem" && objective.type != "giveQuestItem") {
            continue;
        }
        const auto names = namesOf(objective.itemIds, tasks);
        if (names.size() == 1) {
            lines.push_back(withFoundInRaid(
                trf(Str::NeedHandIn, counted(names[0], objective.count).c_str()),
                objective.foundInRaid));
        } else if (names.size() > 1) {
            lines.push_back(withFoundInRaid(
                trf(Str::NeedHandInAnyOf, objective.count > 0 ? objective.count : 1,
                    listOf(names).c_str()),
                objective.foundInRaid));
        }
    }
    return lines;
}

}  // namespace tarkov::ui
