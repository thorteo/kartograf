#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace tarkov::game {

/// A timestamp as the game's logs write it: "2026-09-20 01:36:09.808".
struct LogTimestamp {
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    int millis = 0;

    /// Milliseconds since the Unix epoch, treating the timestamp as UTC.
    ///
    /// The game writes local time with no zone, so this is only meaningful for
    /// ordering and for differences within one session -- which is all we need,
    /// and all it should ever be used for.
    long long millisSinceEpoch() const;
};

/// One parsed record from a log file.
struct LogLine {
    LogTimestamp timestamp;
    std::string_view version;
    std::string_view level;    ///< Info, Debug, Error...
    std::string_view channel;  ///< application, backend, output...
    std::string_view message;
};

/// Splits a raw log line into its five pipe-delimited fields.
///
/// Returns nullopt for a line with no leading timestamp, which means a
/// continuation of the previous record -- the game pretty-prints JSON blocks
/// across many lines, and stack traces likewise.
///
/// A trailing carriage return is stripped: the game writes CRLF, and the
/// fixtures keep it that way deliberately.
std::optional<LogLine> parseLogLine(std::string_view raw);

/// When a session log folder was created, from its name:
/// "log_2026.09.21_19-54-34_1.1.5.1.47510".
///
/// The hour is not zero-padded ("log_2026.09.21_2-31-42_..."), so the names do
/// not sort as strings; compare the parsed times instead. Returns nullopt for
/// anything that is not a session folder.
std::optional<LogTimestamp> parseLogFolderName(std::string_view name);

enum class RaidEventType {
    /// Map assets started loading. The earliest signal we get, roughly 30
    /// seconds before the raid, and the right moment to prefetch.
    MapLoading,
    /// Matchmaking progress. Useful only for showing queue time.
    MatchingStage,
    /// Map finished loading.
    LocationLoaded,
    /// Server assigned. Authoritative map and raid mode.
    RaidCreated,
    /// Raid is live; the in-raid clock starts here.
    RaidStarted,
    /// Result screen shown.
    RaidEnded,
};

struct RaidEvent {
    RaidEventType type{};
    LogTimestamp timestamp;

    /// Internal game map id, e.g. "bigmap". Empty when the event carries none.
    ///
    /// Preserved exactly as written. The game is inconsistent about case --
    /// "bigmap" but "Shoreline" -- so never compare this directly; put it
    /// through geo::mapIdToNormalizedName.
    std::string mapId;

    std::string raidMode;  ///< "Online" for PvP.
    std::string shortId;   ///< The raid code shown in-game.
    std::string stage;     ///< Matching stage letter.
};

/// Recognises raid lifecycle events in a log stream, one line at a time.
///
/// Stateful because it has to be: raid-end is detected from a stack-trace line
/// that carries no timestamp of its own, so the parser remembers the last one
/// it saw.
class LogParser {
public:
    /// Feeds one raw line. Returns an event when the line produced one.
    std::optional<RaidEvent> feedLine(std::string_view raw);

private:
    LogTimestamp lastTimestamp_;
    bool haveTimestamp_ = false;
};

}  // namespace tarkov::game
