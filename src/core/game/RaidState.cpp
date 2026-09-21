#include "core/game/RaidState.h"

#include <algorithm>

#include "core/geo/Heading.h"
#include "core/geo/MapConfig.h"

namespace tarkov::game {

const char* raidPhaseName(RaidPhase phase) {
    switch (phase) {
        case RaidPhase::Idle: return "idle";
        case RaidPhase::Loading: return "loading";
        case RaidPhase::Matching: return "matching";
        case RaidPhase::Loaded: return "loaded";
        case RaidPhase::InRaid: return "in raid";
        case RaidPhase::Ended: return "ended";
    }
    return "unknown";
}

double PositionFix::ageSeconds(std::int64_t nowMillis) const {
    if (!valid) {
        return 0.0;
    }
    return std::max(0.0, static_cast<double>(nowMillis - receivedAtMillis) / 1000.0);
}

double RaidSnapshot::raidElapsedSeconds(std::int64_t nowMillis) const {
    if (phase != RaidPhase::InRaid || raidStartedAtMillis == 0) {
        return 0.0;
    }
    return std::max(0.0,
                    static_cast<double>(nowMillis - raidStartedAtMillis) / 1000.0);
}

void RaidState::setPhase(RaidPhase phase, std::int64_t nowMillis) {
    if (state_.phase == phase) {
        return;
    }
    state_.phase = phase;
    state_.phaseChangedAtMillis = nowMillis;
}

void RaidState::apply(const RaidEvent& event, std::int64_t nowMillis) {
    const std::lock_guard<std::mutex> lock(mutex_);

    switch (event.type) {
        case RaidEventType::MapLoading:
            // A new raid is starting; anything left from the last one is stale.
            state_ = RaidSnapshot{};
            state_.mapId = event.mapId;
            if (const auto normalized = geo::mapIdToNormalizedName(event.mapId)) {
                state_.mapNormalized = std::string(*normalized);
            }
            setPhase(RaidPhase::Loading, nowMillis);
            break;

        case RaidEventType::MatchingStage:
            if (state_.phase == RaidPhase::Loading) {
                setPhase(RaidPhase::Matching, nowMillis);
            }
            break;

        case RaidEventType::LocationLoaded:
            setPhase(RaidPhase::Loaded, nowMillis);
            break;

        case RaidEventType::RaidCreated:
            // Authoritative: this overrides whatever the scene preset implied.
            if (!event.mapId.empty()) {
                state_.mapId = event.mapId;
                state_.mapNormalized.clear();
                if (const auto normalized = geo::mapIdToNormalizedName(event.mapId)) {
                    state_.mapNormalized = std::string(*normalized);
                }
            }
            state_.raidMode = event.raidMode;
            state_.shortId = event.shortId;
            break;

        case RaidEventType::RaidStarted:
            state_.raidStartedAtMillis = nowMillis;
            setPhase(RaidPhase::InRaid, nowMillis);
            break;

        case RaidEventType::RaidEnded:
            setPhase(RaidPhase::Ended, nowMillis);
            state_.fix.valid = false;
            break;
    }

    published_.reset();
}

void RaidState::applyPosition(const ScreenshotName& shot, std::int64_t nowMillis) {
    const std::lock_guard<std::mutex> lock(mutex_);

    state_.fix.valid = true;
    state_.fix.world = shot.position;
    state_.fix.headingDegrees = geo::headingDegrees(shot.orientation);
    state_.fix.gameTimeHours = shot.gameTimeHours;
    state_.fix.receivedAtMillis = nowMillis;
    ++state_.fixCount;

    published_.reset();
}

std::shared_ptr<const RaidSnapshot> RaidState::snapshot() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (!published_) {
        published_ = std::make_shared<const RaidSnapshot>(state_);
    }
    return published_;
}

}  // namespace tarkov::game
