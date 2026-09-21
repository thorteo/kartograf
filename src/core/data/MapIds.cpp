#include "core/data/MapIds.h"

#include <algorithm>

#include <nlohmann/json.hpp>

namespace tarkov::data {

MapIdTable MapIdTable::parse(std::string_view text) {
    MapIdTable table;
    const nlohmann::json root =
        nlohmann::json::parse(text, nullptr, /*allow_exceptions=*/false);
    if (!root.is_object()) {
        return table;
    }
    for (const auto& [id, entry] : root.items()) {
        if (!entry.is_object()) {
            continue;
        }
        Entry parsed;
        parsed.id = id;
        if (const auto it = entry.find("apiName");
            it != entry.end() && it->is_string()) {
            parsed.apiName = it->get<std::string>();
        }
        if (const auto it = entry.find("drawnAs");
            it != entry.end() && it->is_string()) {
            parsed.drawnAs = it->get<std::string>();
        }
        if (!parsed.drawnAs.empty()) {
            table.entries_.push_back(std::move(parsed));
        }
    }
    std::sort(table.entries_.begin(), table.entries_.end(),
              [](const Entry& a, const Entry& b) { return a.id < b.id; });
    return table;
}

std::optional<std::string_view> MapIdTable::drawnAs(std::string_view mapId) const {
    const auto it = std::lower_bound(
        entries_.begin(), entries_.end(), mapId,
        [](const Entry& entry, std::string_view wanted) {
            return entry.id < wanted;
        });
    if (it == entries_.end() || it->id != mapId) {
        return std::nullopt;
    }
    return std::string_view(it->drawnAs);
}

std::vector<std::string_view> MapIdTable::idsFor(std::string_view drawnAsName) const {
    std::vector<std::string_view> ids;
    for (const auto& entry : entries_) {
        if (entry.drawnAs == drawnAsName) {
            ids.emplace_back(entry.id);
        }
    }
    return ids;
}

}  // namespace tarkov::data
