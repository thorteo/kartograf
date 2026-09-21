#include "core/data/QuestProgress.h"

#include <nlohmann/json.hpp>

#include <map>
#include <vector>

namespace tarkov::data {
namespace {

QuestState fromEvent(game::QuestEventKind kind) {
    switch (kind) {
        case game::QuestEventKind::Started: return QuestState::Active;
        case game::QuestEventKind::Completed: return QuestState::Completed;
        case game::QuestEventKind::Failed: return QuestState::Failed;
    }
    return QuestState::Unknown;
}

std::optional<QuestState> stateFromName(std::string_view name) {
    if (name == "active") return QuestState::Active;
    if (name == "completed") return QuestState::Completed;
    if (name == "failed") return QuestState::Failed;
    if (name == "unknown") return QuestState::Unknown;
    return std::nullopt;
}

}  // namespace

const char* questStateName(QuestState state) {
    switch (state) {
        case QuestState::Unknown: return "unknown";
        case QuestState::Active: return "active";
        case QuestState::Completed: return "completed";
        case QuestState::Failed: return "failed";
    }
    return "unknown";
}

bool QuestProgress::apply(const game::QuestEvent& event) {
    if (event.questId.empty()) {
        return false;
    }
    const QuestState state = fromEvent(event.kind);
    auto it = records_.find(event.questId);
    if (it == records_.end()) {
        records_.emplace(event.questId, QuestRecord{state, event.unixTime, false});
        return true;
    }

    QuestRecord& record = it->second;
    // Older news never overwrites newer. Equal times favour the later stage of
    // the lifecycle, since a start and a completion can share a second.
    if (event.unixTime < record.since) {
        return false;
    }
    if (event.unixTime == record.since &&
        !(record.state == QuestState::Active && state != QuestState::Active)) {
        return false;
    }
    if (record.state == state && !record.manual) {
        record.since = event.unixTime;
        return false;
    }
    record = QuestRecord{state, event.unixTime, false};
    return true;
}

void QuestProgress::setManual(const std::string& questId, QuestState state,
                              std::int64_t now) {
    records_[questId] = QuestRecord{state, now, true};
}

void QuestProgress::clearManual(const std::string& questId) {
    const auto it = records_.find(questId);
    if (it != records_.end() && it->second.manual) {
        records_.erase(it);
    }
}

const QuestRecord* QuestProgress::find(std::string_view questId) const {
    const auto it = records_.find(questId);
    return it == records_.end() ? nullptr : &it->second;
}

std::string QuestProgress::serialize() const {
    nlohmann::ordered_json quests = nlohmann::ordered_json::object();
    for (const auto& [id, record] : records_) {
        quests[id] = {{"state", questStateName(record.state)},
                      {"since", record.since},
                      {"manual", record.manual}};
    }
    nlohmann::ordered_json root;
    root["version"] = 1;
    root["quests"] = std::move(quests);
    return root.dump(1);
}

QuestProgress QuestProgress::parse(std::string_view text) {
    QuestProgress progress;
    const auto root = nlohmann::json::parse(text, nullptr, /*allow_exceptions=*/false);
    if (!root.is_object()) {
        return progress;
    }
    const auto quests = root.find("quests");
    if (quests == root.end() || !quests->is_object()) {
        return progress;
    }
    for (const auto& [id, entry] : quests->items()) {
        if (!entry.is_object()) {
            continue;
        }
        const auto stateIt = entry.find("state");
        if (stateIt == entry.end() || !stateIt->is_string()) {
            continue;
        }
        const auto state = stateFromName(stateIt->get<std::string>());
        if (!state) {
            continue;
        }
        QuestRecord record;
        record.state = *state;
        if (const auto since = entry.find("since");
            since != entry.end() && since->is_number()) {
            record.since = since->get<std::int64_t>();
        }
        if (const auto manual = entry.find("manual");
            manual != entry.end() && manual->is_boolean()) {
            record.manual = manual->get<bool>();
        }
        progress.records_.emplace(id, record);
    }
    return progress;
}

QuestState EffectiveProgress::stateOf(std::string_view questId) const {
    if (active.count(questId) != 0) return QuestState::Active;
    if (completed.count(questId) != 0) return QuestState::Completed;
    if (failed.count(questId) != 0) return QuestState::Failed;
    return QuestState::Unknown;
}

EffectiveProgress resolveProgress(const QuestProgress& progress, const TaskSet& tasks) {
    EffectiveProgress out;
    for (const auto& [id, record] : progress.records()) {
        switch (record.state) {
            case QuestState::Active: out.active.insert(id); break;
            case QuestState::Completed: out.completed.insert(id); break;
            case QuestState::Failed: out.failed.insert(id); break;
            case QuestState::Unknown: break;
        }
    }

    std::map<std::string_view, const Task*> byId;
    for (const auto& task : tasks.tasks) {
        byId.emplace(task.id, &task);
    }

    // Walk back from everything started or finished. Its prerequisites were
    // necessarily completed, whether or not the game still has the logs.
    std::vector<std::string_view> frontier;
    for (const auto& id : out.active) frontier.push_back(id);
    for (const auto& id : out.completed) frontier.push_back(id);

    std::set<std::string_view> visited;
    while (!frontier.empty()) {
        const std::string_view id = frontier.back();
        frontier.pop_back();
        if (!visited.insert(id).second) {
            continue;
        }
        const auto it = byId.find(id);
        if (it == byId.end()) {
            continue;
        }
        for (const auto& prerequisite : it->second->prerequisiteIds) {
            // An explicit record wins over inference, in both directions.
            if (progress.find(prerequisite) != nullptr) {
                if (out.completed.count(prerequisite) != 0) {
                    frontier.push_back(prerequisite);
                }
                continue;
            }
            if (out.completed.insert(prerequisite).second) {
                out.inferredCompleted.insert(prerequisite);
            }
            frontier.push_back(prerequisite);
        }
    }
    return out;
}

QuestProgressStore::QuestProgressStore()
    : published_(std::make_shared<const QuestProgress>()) {}

template <typename Fn>
bool QuestProgressStore::mutate(Fn&& fn) {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (!fn(current_)) {
        return false;
    }
    published_ = std::make_shared<const QuestProgress>(current_);
    ++revision_;
    return true;
}

bool QuestProgressStore::apply(const game::QuestEvent& event) {
    return mutate([&](QuestProgress& p) { return p.apply(event); });
}

void QuestProgressStore::setManual(const std::string& questId, QuestState state,
                                   std::int64_t now) {
    mutate([&](QuestProgress& p) {
        p.setManual(questId, state, now);
        return true;
    });
}

void QuestProgressStore::clearManual(const std::string& questId) {
    mutate([&](QuestProgress& p) {
        const bool had = p.find(questId) != nullptr && p.find(questId)->manual;
        p.clearManual(questId);
        return had;
    });
}

void QuestProgressStore::replace(QuestProgress progress) {
    mutate([&](QuestProgress& p) {
        p = std::move(progress);
        return true;
    });
}

std::shared_ptr<const QuestProgress> QuestProgressStore::snapshot() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return published_;
}

std::uint64_t QuestProgressStore::revision() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return revision_;
}

}  // namespace tarkov::data
