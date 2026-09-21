#include "core/game/QuestEvents.h"

#include <nlohmann/json.hpp>

namespace tarkov::game {
namespace {

constexpr std::string_view kNotificationHeader = "Got notification";

bool isHex(std::string_view s) {
    for (const char c : s) {
        const bool digit = c >= '0' && c <= '9';
        const bool lower = c >= 'a' && c <= 'f';
        if (!digit && !lower) {
            return false;
        }
    }
    return true;
}

}  // namespace

std::optional<QuestEvent> parseQuestNotification(std::string_view text) {
    const auto root = nlohmann::json::parse(text, nullptr, /*allow_exceptions=*/false);
    if (!root.is_object()) {
        return std::nullopt;
    }
    const auto messageIt = root.find("message");
    if (messageIt == root.end() || !messageIt->is_object()) {
        return std::nullopt;
    }
    const auto& message = *messageIt;

    const auto typeIt = message.find("type");
    const auto templateIt = message.find("templateId");
    if (typeIt == message.end() || !typeIt->is_number_integer() ||
        templateIt == message.end() || !templateIt->is_string()) {
        return std::nullopt;
    }

    // "<questId> <suffix>", and occasionally more after the suffix.
    const std::string templateId = templateIt->get<std::string>();
    const std::size_t space = templateId.find(' ');
    if (space == std::string::npos) {
        return std::nullopt;
    }
    const std::string_view questId(templateId.data(), space);
    std::string_view suffix(templateId.data() + space + 1, templateId.size() - space - 1);
    if (const std::size_t more = suffix.find(' '); more != std::string_view::npos) {
        suffix = suffix.substr(0, more);
    }
    if (questId.size() != 24 || !isHex(questId)) {
        return std::nullopt;
    }

    const int type = typeIt->get<int>();
    QuestEvent event;
    if (type == 10 && suffix == "description") {
        event.kind = QuestEventKind::Started;
    } else if (type == 12 && suffix == "successMessageText") {
        event.kind = QuestEventKind::Completed;
    } else if (type == 11 && suffix == "failMessageText") {
        event.kind = QuestEventKind::Failed;
    } else {
        return std::nullopt;
    }
    event.questId = std::string(questId);

    if (const auto dt = message.find("dt"); dt != message.end() && dt->is_number()) {
        event.unixTime = dt->get<std::int64_t>();
    }
    return event;
}

std::optional<QuestEvent> QuestEventParser::finishRecord() {
    collecting_ = false;
    depth_ = 0;
    inString_ = false;
    escaped_ = false;
    std::string body;
    body.swap(body_);
    return parseQuestNotification(body);
}

std::optional<QuestEvent> QuestEventParser::feedLine(std::string_view raw) {
    if (!raw.empty() && raw.back() == '\r') {
        raw.remove_suffix(1);
    }

    if (parseLogLine(raw)) {
        // A new timestamped record. Anything half-collected is abandoned: a
        // truncated body cannot be trusted.
        collecting_ = false;
        body_.clear();
        depth_ = 0;
        inString_ = false;
        escaped_ = false;
        if (raw.find(kNotificationHeader) != std::string_view::npos) {
            collecting_ = true;
        }
        return std::nullopt;
    }
    if (!collecting_) {
        return std::nullopt;
    }

    // Track braces outside of strings so the body is known to be complete
    // exactly when its outer object closes, without waiting for the next
    // record -- which may not arrive until the next raid.
    for (const char c : raw) {
        if (inString_) {
            if (escaped_) {
                escaped_ = false;
            } else if (c == '\\') {
                escaped_ = true;
            } else if (c == '"') {
                inString_ = false;
            }
            continue;
        }
        if (c == '"') {
            inString_ = true;
        } else if (c == '{') {
            ++depth_;
        } else if (c == '}') {
            --depth_;
        }
    }
    body_.append(raw);
    body_.push_back('\n');

    if (depth_ == 0 && body_.find('{') != std::string::npos) {
        return finishRecord();
    }
    if (depth_ < 0 || body_.size() > 256 * 1024) {
        // Malformed or runaway; drop it rather than grow without bound.
        collecting_ = false;
        body_.clear();
        depth_ = 0;
    }
    return std::nullopt;
}

}  // namespace tarkov::game
