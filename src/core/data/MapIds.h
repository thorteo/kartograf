#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tarkov::data {

/// Translates the BSG map ids the task API uses into the names the renderer
/// draws by.
///
/// Several ids are variants of one drawable map -- night Factory, the level-21
/// and tutorial versions of Ground Zero, the dark Lab -- and they share a
/// picture, so a quest on any of them belongs on the same map.
class MapIdTable {
public:
    /// Parses the pinned data/map-ids.json.
    static MapIdTable parse(std::string_view json);

    /// The tarkov.dev map name to draw a quest on, or nullopt for an id we do
    /// not know. Callers should report an unknown id rather than guess: a new
    /// map silently drawn as the wrong one is worse than one drawn not at all.
    std::optional<std::string_view> drawnAs(std::string_view mapId) const;

    /// Every id that draws as the given map name. A quest set has to be
    /// filtered by all of them, or quests on night Factory would vanish when
    /// the player is on Factory.
    std::vector<std::string_view> idsFor(std::string_view drawnAsName) const;

    std::size_t size() const { return entries_.size(); }

private:
    struct Entry {
        std::string id;
        std::string apiName;
        std::string drawnAs;
    };
    std::vector<Entry> entries_;  ///< sorted by id
};

/// The pinned table, compiled into the binary.
extern const std::string_view kMapIdsJson;

}  // namespace tarkov::data
