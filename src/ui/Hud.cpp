#include "ui/Hud.h"

#include <imgui.h>

#include <cmath>
#include <cstdio>

namespace tarkov::ui {
namespace {

constexpr ImVec4 kFresh{0.72f, 0.82f, 0.50f, 1.0f};
constexpr ImVec4 kAging{0.88f, 0.78f, 0.42f, 1.0f};
constexpr ImVec4 kStale{0.85f, 0.48f, 0.42f, 1.0f};
constexpr ImVec4 kMuted{0.55f, 0.57f, 0.52f, 1.0f};

/// A position fix is a ping, not a stream. The colour has to say so before the
/// player trusts a marker that is two minutes old.
ImVec4 confidenceColour(double ageSeconds) {
    if (ageSeconds < 20.0) return kFresh;
    if (ageSeconds < 60.0) return kAging;
    return kStale;
}

void labelled(const char* label, const char* value, ImVec4 colour) {
    ImGui::TextColored(kMuted, "%s", label);
    ImGui::SameLine();
    ImGui::TextColored(colour, "%s", value);
}

const char* compassPoint(double degrees) {
    static const char* kPoints[] = {"N",  "NE", "E",  "SE",
                                    "S",  "SW", "W",  "NW"};
    const int index = static_cast<int>(std::lround(degrees / 45.0)) & 7;
    return kPoints[index];
}

}  // namespace

void formatDuration(double seconds, char* buffer, std::size_t size) {
    if (seconds < 0.0) {
        seconds = 0.0;
    }
    const int total = static_cast<int>(seconds);
    const int hours = total / 3600;
    const int minutes = (total % 3600) / 60;
    const int secs = total % 60;
    if (hours > 0) {
        std::snprintf(buffer, size, "%d:%02d:%02d", hours, minutes, secs);
    } else {
        std::snprintf(buffer, size, "%d:%02d", minutes, secs);
    }
}

void formatGameClock(double gameTimeHours, char* buffer, std::size_t size) {
    // 14.65 means 14:39 -- the fractional part is a fraction of an hour, not
    // minutes. See docs/research/eft-screenshot-format.md.
    double hours = std::fmod(gameTimeHours, 24.0);
    if (hours < 0.0) {
        hours += 24.0;
    }
    int whole = static_cast<int>(hours);
    int minutes = static_cast<int>(std::lround((hours - whole) * 60.0));
    if (minutes == 60) {
        minutes = 0;
        whole = (whole + 1) % 24;
    }
    std::snprintf(buffer, size, "%02d:%02d", whole, minutes);
}

void drawHud(const game::RaidSnapshot& snapshot, std::int64_t nowMillis,
             const HudOptions& options) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float pad = 16.0f;

    ImVec2 position = viewport->WorkPos;
    ImVec2 pivot{0.0f, 0.0f};
    if (options.corner & 1) {
        position.x += viewport->WorkSize.x - pad;
        pivot.x = 1.0f;
    } else {
        position.x += pad;
    }
    if (options.corner & 2) {
        position.y += viewport->WorkSize.y - pad;
        pivot.y = 1.0f;
    } else {
        position.y += pad;
    }
    ImGui::SetNextWindowPos(position, ImGuiCond_Always, pivot);
    ImGui::SetNextWindowBgAlpha(0.82f * options.opacity);

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;

    if (!ImGui::Begin("tarkov-help-hud", nullptr, flags)) {
        ImGui::End();
        return;
    }

    char buffer[64];

    // Map
    if (!snapshot.mapNormalized.empty()) {
        labelled("map", snapshot.mapNormalized.c_str(), kFresh);
    } else if (!snapshot.mapId.empty()) {
        // An id we do not recognise is worth showing rather than hiding: it
        // means a new map shipped and the lookup table needs an entry.
        labelled("map", snapshot.mapId.c_str(), kAging);
        ImGui::TextColored(kStale, "unrecognised map id");
    } else {
        labelled("map", "--", kMuted);
    }

    labelled("state", game::raidPhaseName(snapshot.phase), kMuted);

    if (snapshot.phase == game::RaidPhase::InRaid) {
        formatDuration(snapshot.raidElapsedSeconds(nowMillis), buffer, sizeof(buffer));
        labelled("elapsed", buffer, kFresh);
    }

    ImGui::Separator();

    if (snapshot.fix.valid) {
        const double age = snapshot.fix.ageSeconds(nowMillis);
        const ImVec4 colour = confidenceColour(age);

        std::snprintf(buffer, sizeof(buffer), "%.0f, %.0f",
                      snapshot.fix.world.x, snapshot.fix.world.z);
        labelled("pos", buffer, colour);

        std::snprintf(buffer, sizeof(buffer), "%.0f m", snapshot.fix.world.y);
        labelled("elev", buffer, colour);

        std::snprintf(buffer, sizeof(buffer), "%s  %.0f deg",
                      compassPoint(snapshot.fix.headingDegrees),
                      snapshot.fix.headingDegrees);
        labelled("facing", buffer, colour);

        formatGameClock(snapshot.fix.gameTimeHours, buffer, sizeof(buffer));
        labelled("in-game", buffer, kMuted);

        formatDuration(age, buffer, sizeof(buffer));
        labelled("fix age", buffer, colour);
        if (age >= 60.0) {
            ImGui::TextColored(kStale, "press the screenshot key to update");
        }
    } else {
        ImGui::TextColored(kMuted, "no position yet");
        ImGui::TextColored(kMuted, "press the in-game screenshot key");
    }

    if (options.showDebug) {
        ImGui::Separator();
        std::snprintf(buffer, sizeof(buffer), "%d", snapshot.fixCount);
        labelled("fixes", buffer, kMuted);
        if (!snapshot.shortId.empty()) {
            labelled("raid", snapshot.shortId.c_str(), kMuted);
        }
        ImGui::TextColored(kMuted, "%.0f fps", ImGui::GetIO().Framerate);
    }

    ImGui::End();
}

}  // namespace tarkov::ui
