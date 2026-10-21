#include "core/game/LogParser.h"

#include <charconv>
#include <cstddef>

namespace tarkov::game {
namespace {

constexpr std::string_view kScenePreset = "scene preset path:";
constexpr std::string_view kRcid = "rcid:";
constexpr std::string_view kMatching = "TRACE-NetworkGameMatching ";
constexpr std::string_view kLocationLoaded = "LocationLoaded:";
constexpr std::string_view kGameCreate = "TRACE-NetworkGameCreate profileStatus:";
constexpr std::string_view kTransit = "[Transit] ";
constexpr std::string_view kTransitLocations = "Locations:";
constexpr std::string_view kGameStarted = "GameStarted:";
constexpr std::string_view kSessionResult = "EFT.SessionResultShowOperation:Execute(";

bool parseFixedInt(std::string_view s, std::size_t offset, std::size_t width, int& out) {
    if (offset + width > s.size()) {
        return false;
    }
    for (std::size_t i = 0; i < width; ++i) {
        const char c = s[offset + i];
        if (c < '0' || c > '9') {
            return false;
        }
    }
    const char* begin = s.data() + offset;
    return std::from_chars(begin, begin + width, out).ec == std::errc{};
}

/// Reads one to `maxWidth` digits at `cursor`, then expects `separator`.
/// Advances past both. Used where the game does not zero-pad.
bool parseDelimitedInt(std::string_view s, std::size_t& cursor, std::size_t maxWidth,
                       char separator, int& out) {
    std::size_t width = 0;
    while (cursor + width < s.size() && width < maxWidth && s[cursor + width] >= '0' &&
           s[cursor + width] <= '9') {
        ++width;
    }
    if (width == 0 || cursor + width >= s.size() || s[cursor + width] != separator) {
        return false;
    }
    const char* begin = s.data() + cursor;
    if (std::from_chars(begin, begin + width, out).ec != std::errc{}) {
        return false;
    }
    cursor += width + 1;
    return true;
}

/// Reads "Key: value" out of the comma-separated profileStatus blob.
std::string fieldValue(std::string_view blob, std::string_view key) {
    const std::size_t at = blob.find(key);
    if (at == std::string_view::npos) {
        return {};
    }
    std::size_t start = at + key.size();
    while (start < blob.size() && (blob[start] == ' ' || blob[start] == ':')) {
        ++start;
    }
    std::size_t end = blob.find_first_of(",'", start);
    if (end == std::string_view::npos) {
        end = blob.size();
    }
    while (end > start && blob[end - 1] == ' ') {
        --end;
    }
    return std::string(blob.substr(start, end - start));
}

/// Days since 1970-01-01 from a civil date. Howard Hinnant's algorithm.
long long daysFromCivil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const long long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153u * (m + (m > 2 ? -3u : 9u)) + 2u) / 5u + d - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097LL + static_cast<long long>(doe) - 719468LL;
}

}  // namespace

long long LogTimestamp::millisSinceEpoch() const {
    const long long days = daysFromCivil(year, static_cast<unsigned>(month),
                                         static_cast<unsigned>(day));
    const long long seconds = days * 86400LL + hour * 3600LL + minute * 60LL + second;
    return seconds * 1000LL + millis;
}

std::optional<LogTimestamp> parseLogFolderName(std::string_view name) {
    constexpr std::string_view kPrefix = "log_";
    if (name.substr(0, kPrefix.size()) != kPrefix) {
        return std::nullopt;
    }
    LogTimestamp time;
    std::size_t cursor = kPrefix.size();
    if (!parseDelimitedInt(name, cursor, 4, '.', time.year) ||
        !parseDelimitedInt(name, cursor, 2, '.', time.month) ||
        !parseDelimitedInt(name, cursor, 2, '_', time.day) ||
        !parseDelimitedInt(name, cursor, 2, '-', time.hour) ||
        !parseDelimitedInt(name, cursor, 2, '-', time.minute) ||
        !parseDelimitedInt(name, cursor, 2, '_', time.second)) {
        return std::nullopt;
    }
    if (time.month < 1 || time.month > 12 || time.day < 1 || time.day > 31 ||
        time.hour > 23 || time.minute > 59 || time.second > 59) {
        return std::nullopt;
    }
    return time;
}

std::optional<LogLine> parseLogLine(std::string_view raw) {
    if (!raw.empty() && raw.back() == '\r') {
        raw.remove_suffix(1);
    }

    LogLine line;
    // "2026-09-20 01:36:09.808|" -- length first: the separators below are
    // read by index, one past what each number's own check covers.
    constexpr std::size_t kTimestampAndPipe = 24;
    if (raw.size() < kTimestampAndPipe) {
        return std::nullopt;
    }
    if (!parseFixedInt(raw, 0, 4, line.timestamp.year) || raw[4] != '-' ||
        !parseFixedInt(raw, 5, 2, line.timestamp.month) || raw[7] != '-' ||
        !parseFixedInt(raw, 8, 2, line.timestamp.day) || raw[10] != ' ' ||
        !parseFixedInt(raw, 11, 2, line.timestamp.hour) || raw[13] != ':' ||
        !parseFixedInt(raw, 14, 2, line.timestamp.minute) || raw[16] != ':' ||
        !parseFixedInt(raw, 17, 2, line.timestamp.second) || raw[19] != '.' ||
        !parseFixedInt(raw, 20, 3, line.timestamp.millis)) {
        return std::nullopt;
    }

    std::size_t cursor = 23;
    if (raw[cursor] != '|') {
        return std::nullopt;
    }

    // Three more delimited fields, then the message -- which may itself contain
    // pipes, so the split stops after four.
    std::string_view* const fields[] = {&line.version, &line.level, &line.channel};
    for (auto* field : fields) {
        const std::size_t start = cursor + 1;
        const std::size_t end = raw.find('|', start);
        if (end == std::string_view::npos) {
            return std::nullopt;
        }
        *field = raw.substr(start, end - start);
        cursor = end;
    }
    line.message = raw.substr(cursor + 1);
    return line;
}

std::optional<RaidEvent> LogParser::feedLine(std::string_view raw) {
    const auto parsed = parseLogLine(raw);

    if (parsed) {
        lastTimestamp_ = parsed->timestamp;
        haveTimestamp_ = true;
    } else {
        // A continuation line. The only one we care about is the stack trace
        // that betrays the end-of-raid result screen. This is a symbol name
        // rather than a purpose-built log message, so it is the most fragile
        // signal we rely on and needs a timeout fallback upstream.
        if (haveTimestamp_ && raw.find(kSessionResult) != std::string_view::npos) {
            RaidEvent event;
            event.type = RaidEventType::RaidEnded;
            event.timestamp = lastTimestamp_;
            return event;
        }
        return std::nullopt;
    }

    const std::string_view message = parsed->message;
    RaidEvent event;
    event.timestamp = parsed->timestamp;

    if (const std::size_t at = message.find(kScenePreset); at != std::string_view::npos) {
        // "...rcid:bigmap.scenespreset.asset"
        const std::size_t rcid = message.find(kRcid, at);
        if (rcid == std::string_view::npos) {
            return std::nullopt;
        }
        const std::size_t start = rcid + kRcid.size();
        const std::size_t dot = message.find('.', start);
        event.type = RaidEventType::MapLoading;
        event.mapId = std::string(message.substr(
            start, dot == std::string_view::npos ? std::string_view::npos : dot - start));
        return event;
    }

    if (const std::size_t at = message.find(kMatching); at != std::string_view::npos) {
        event.type = RaidEventType::MatchingStage;
        event.stage = std::string(message.substr(at + kMatching.size()));
        return event;
    }

    if (message.find(kGameCreate) != std::string_view::npos) {
        event.type = RaidEventType::RaidCreated;
        event.mapId = fieldValue(message, "Location");
        event.raidMode = fieldValue(message, "RaidMode");
        event.shortId = fieldValue(message, "shortId");
        return event;
    }

    if (message.find(kTransit) != std::string_view::npos) {
        // Offline: "[Transit] Flag:None, RaidId:..., Count:0, Locations:TarkovStreets -> "
        // Online the line reads "[Transit] `<profile id>` Count:0, ..." and
        // names no location.
        const std::size_t at = message.find(kTransitLocations);
        if (at == std::string_view::npos) {
            return std::nullopt;
        }
        const std::size_t start = at + kTransitLocations.size();
        const std::size_t end = message.find(' ', start);
        event.mapId = std::string(message.substr(
            start, end == std::string_view::npos ? std::string_view::npos : end - start));
        if (event.mapId.empty()) {
            return std::nullopt;
        }
        event.type = RaidEventType::LocationNamed;
        return event;
    }

    if (message.find(kLocationLoaded) != std::string_view::npos) {
        event.type = RaidEventType::LocationLoaded;
        return event;
    }

    if (message.find(kGameStarted) != std::string_view::npos) {
        event.type = RaidEventType::RaidStarted;
        return event;
    }

    if (message.find(kSessionResult) != std::string_view::npos) {
        event.type = RaidEventType::RaidEnded;
        return event;
    }

    return std::nullopt;
}

}  // namespace tarkov::game
