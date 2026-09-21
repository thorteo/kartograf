#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <string_view>

#include "core/data/TaskModel.h"
#include "core/game/QuestEvents.h"

namespace tarkov::data {

enum class QuestState { Unknown, Active, Completed, Failed };

const char* questStateName(QuestState state);

/// What we know about one quest, and where the knowledge came from.
struct QuestRecord {
    QuestState state = QuestState::Unknown;
    /// When the state was established, in Unix seconds.
    std::int64_t since = 0;
    /// Set by the player rather than read from the game's logs.
    bool manual = false;
};

/// The player's quest progress, merged from every source.
///
/// Three sources feed it: quest events in the game's own logs, which are the
/// ground truth but only cover the handful of days the game keeps logs for;
/// inference from prerequisites, which recovers history the logs have lost;
/// and the player's own corrections for anything neither can see.
///
/// Events are ordered by their own timestamps, so importing an old log after a
/// newer one can never roll a quest back.
class QuestProgress {
public:
    /// Merges a log event. Returns true if anything changed.
    ///
    /// A manual record is only overridden by an event newer than it: the
    /// player's correction stands until the game says something new.
    bool apply(const game::QuestEvent& event);

    /// Records a correction from the player.
    void setManual(const std::string& questId, QuestState state, std::int64_t now);

    /// Drops a correction, returning the quest to whatever the logs say.
    void clearManual(const std::string& questId);

    const QuestRecord* find(std::string_view questId) const;
    const std::map<std::string, QuestRecord, std::less<>>& records() const {
        return records_;
    }

    /// Serialised for disk. Stable key order, so the file diffs cleanly.
    std::string serialize() const;
    static QuestProgress parse(std::string_view json);

private:
    std::map<std::string, QuestRecord, std::less<>> records_;
};

/// Progress with inference applied: what the overlay actually uses.
struct EffectiveProgress {
    std::set<std::string, std::less<>> active;
    std::set<std::string, std::less<>> completed;
    std::set<std::string, std::less<>> failed;
    /// Completed only by inference, never seen in a log or marked by hand.
    /// Kept apart so the UI can say "assumed" rather than claim certainty.
    std::set<std::string, std::less<>> inferredCompleted;

    QuestState stateOf(std::string_view questId) const;
};

/// Resolves recorded progress against the quest graph.
///
/// Anything a started or completed quest required must itself have been
/// completed, transitively. An inferred completion never overrides an explicit
/// record: if the player marked a prerequisite as not done, that stands.
EffectiveProgress resolveProgress(const QuestProgress& progress, const TaskSet& tasks);

/// A thread-safe holder: one writer at a time, readers take an immutable
/// snapshot. Follows the pattern of docs/adr/0004-threading-model.md.
class QuestProgressStore {
public:
    QuestProgressStore();

    bool apply(const game::QuestEvent& event);
    void setManual(const std::string& questId, QuestState state, std::int64_t now);
    void clearManual(const std::string& questId);
    void replace(QuestProgress progress);

    std::shared_ptr<const QuestProgress> snapshot() const;

    /// Increments on every change. Lets a caller persist or rebuild only when
    /// something actually moved.
    std::uint64_t revision() const;

private:
    template <typename Fn>
    bool mutate(Fn&& fn);

    mutable std::mutex mutex_;
    QuestProgress current_;
    std::shared_ptr<const QuestProgress> published_;
    std::uint64_t revision_ = 0;
};

}  // namespace tarkov::data
