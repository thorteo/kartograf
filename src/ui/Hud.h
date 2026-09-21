#pragma once

#include <cstdint>

#include "core/game/RaidState.h"

namespace tarkov::ui {

struct HudOptions {
    float opacity = 1.0f;
    bool showDebug = false;
    /// Corner of the game window: 0 top-left, 1 top-right, 2 bottom-left,
    /// 3 bottom-right.
    int corner = 1;
};

/// Draws the always-available heads-up panel.
///
/// Deliberately small and cheap. The full map is a separate, on-demand surface:
/// compositing a fullscreen transparent window is the most expensive thing the
/// overlay can do, so the thing that is always on screen must not be that.
void drawHud(const game::RaidSnapshot& snapshot, std::int64_t nowMillis,
             const HudOptions& options);

/// Formats seconds as m:ss, or h:mm:ss past an hour.
void formatDuration(double seconds, char* buffer, std::size_t size);

/// Formats the game's fractional in-game hour as HH:MM.
void formatGameClock(double gameTimeHours, char* buffer, std::size_t size);

}  // namespace tarkov::ui
