#pragma once

#include <optional>
#include <string_view>

#include "core/geo/Vec.h"

namespace tarkov::game {

/// Everything Escape from Tarkov encodes into a screenshot's filename.
///
/// This is how the overlay learns where the player is without going anywhere
/// near the game process. See docs/research/eft-screenshot-format.md.
struct ScreenshotName {
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;    ///< Wall clock, minute resolution only.
    int minute = 0;

    geo::Vec3 position;
    geo::Quat orientation;

    /// In-game time of day, in hours. 14.65 means 14:39.
    ///
    /// Verified empirically: the game's day cycle runs at roughly 7x real time,
    /// and successive screenshots advance this value at a measured median of
    /// 6.80x wall clock. See the research note for the method.
    double gameTimeHours = 0.0;

    /// Collision counter the game appends when a name is already taken. Also
    /// the only way to order two shots taken within the same minute, since the
    /// timestamp has no seconds.
    int sequence = 0;
};

/// Parses a screenshot filename. Returns nullopt if it does not match the
/// grammar exactly.
///
/// Strict by design: all 741 filenames in the reference corpus conform, so a
/// name that does not is a signal the format changed, not a case to paper over.
/// Pass the filename only, not a full path.
std::optional<ScreenshotName> parseScreenshotName(std::string_view filename);

}  // namespace tarkov::game
