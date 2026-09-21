#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

#include "core/game/LogParser.h"
#include "core/game/ScreenshotName.h"
#include "core/geo/Vec.h"

namespace tarkov::game {

enum class RaidPhase {
    Idle,      ///< Not in or near a raid.
    Loading,   ///< Map assets are loading; roughly 30 seconds of warning.
    Matching,  ///< Queueing for a server.
    Loaded,    ///< Map loaded, raid not yet live.
    InRaid,    ///< Live. The in-raid clock runs from here.
    Ended,     ///< Result screen shown.
};

/// Human-readable phase name.
///
/// Deliberately not called toString: doctest looks up an ADL toString() for
/// stringification and expects it to return doctest::String, so a core API by
/// that name silently breaks every CHECK involving this type.
const char* raidPhaseName(RaidPhase phase);

/// The last known player position.
///
/// A position is a *ping*, not a stream: it only arrives when the player
/// presses the screenshot key. Everything that consumes this has to respect
/// that, which is why the age is part of the data rather than something the UI
/// has to remember to compute.
struct PositionFix {
    bool valid = false;
    geo::Vec3 world;
    double headingDegrees = 0.0;
    double gameTimeHours = 0.0;
    std::int64_t receivedAtMillis = 0;

    /// Seconds since this fix arrived. Negative clock movement is clamped away.
    double ageSeconds(std::int64_t nowMillis) const;
};

/// An immutable view of the world, handed to the renderer once per frame.
struct RaidSnapshot {
    RaidPhase phase = RaidPhase::Idle;

    std::string mapId;          ///< Raw from the game's log, original casing.
    std::string mapNormalized;  ///< tarkov.dev name; empty if unrecognised.
    std::string raidMode;
    std::string shortId;

    std::int64_t phaseChangedAtMillis = 0;
    std::int64_t raidStartedAtMillis = 0;

    PositionFix fix;
    int fixCount = 0;

    /// Seconds since the raid went live, or 0 when not in a raid.
    double raidElapsedSeconds(std::int64_t nowMillis) const;
};

/// The single piece of shared mutable state in the application.
///
/// One mutex, one writer, and readers get an immutable snapshot. Nothing else
/// crosses a thread boundary mutably. See docs/adr/0004-threading-model.md
/// before adding a field here, and do not add shared state anywhere else.
class RaidState {
public:
    /// Applies a log event. Writer thread only.
    void apply(const RaidEvent& event, std::int64_t nowMillis);

    /// Applies a position fix parsed from a screenshot filename. Writer only.
    ///
    /// Fixes arriving while not in a raid are still recorded: the game writes
    /// the screenshot before our log tail necessarily catches up, and dropping
    /// them would lose the first fix of every raid.
    void applyPosition(const ScreenshotName& shot, std::int64_t nowMillis);

    /// A coherent, immutable view. Safe from any thread.
    ///
    /// Cheap when nothing changed: the same snapshot is handed out until the
    /// next write, so an idle overlay allocates nothing per frame.
    std::shared_ptr<const RaidSnapshot> snapshot() const;

private:
    void setPhase(RaidPhase phase, std::int64_t nowMillis);

    mutable std::mutex mutex_;
    RaidSnapshot state_;
    mutable std::shared_ptr<const RaidSnapshot> published_;
};

}  // namespace tarkov::game
