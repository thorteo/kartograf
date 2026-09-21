#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "core/game/LogParser.h"

namespace tarkov::game {

enum class QuestEventKind { Started, Completed, Failed };

/// A quest changing state, as the game reports it to itself.
struct QuestEvent {
    QuestEventKind kind{};
    std::string questId;
    /// Server time of the message, in Unix seconds. Used to order events so
    /// that importing old logs after new ones cannot resurrect a stale state.
    std::int64_t unixTime = 0;
};

/// Recognises quest events in `push-notifications_000.log`.
///
/// Trader messages about quests arrive as chat notifications whose JSON body
/// spans many lines. The meaning is carried by the message type and by the
/// suffix of `templateId`:
///
///   type 10, "<questId> description"          quest started
///   type 12, "<questId> successMessageText"   quest completed
///   type 11, "<questId> failMessageText"      quest failed
///
/// The `text` field is **not** a reliable signal: completion messages carry
/// "quest started" in it as well. A parser that trusted `text` would record
/// every completion as a start. See docs/research/eft-log-format.md.
class QuestEventParser {
public:
    /// Feeds one raw line. Returns an event when a record just completed and
    /// was a quest event.
    std::optional<QuestEvent> feedLine(std::string_view raw);

private:
    std::optional<QuestEvent> finishRecord();

    bool collecting_ = false;
    std::string body_;
    int depth_ = 0;
    bool inString_ = false;
    bool escaped_ = false;
};

/// Parses a single notification JSON body. Exposed for tests.
std::optional<QuestEvent> parseQuestNotification(std::string_view json);

}  // namespace tarkov::game
