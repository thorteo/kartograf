#include "ui/MapView.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core/geo/Heading.h"
#include "core/geo/Projection.h"

namespace tarkov::ui {
namespace {

constexpr double kMinScale = 0.05;
constexpr double kMaxScale = 24.0;

ImU32 rgba(float r, float g, float b, float a) {
    return ImGui::GetColorU32(ImVec4(r, g, b, a));
}

ImVec2 toImVec(const geo::Vec2& v) {
    return ImVec2(static_cast<float>(v.x), static_cast<float>(v.y));
}

/// Colour for the player marker, fading as the fix ages. A stale position must
/// not look like a live one.
ImU32 markerColour(double ageSeconds) {
    if (ageSeconds < 20.0) return rgba(0.72f, 0.88f, 0.45f, 1.0f);
    if (ageSeconds < 60.0) return rgba(0.92f, 0.80f, 0.38f, 0.92f);
    return rgba(0.88f, 0.45f, 0.40f, 0.80f);
}

void drawPlayerMarker(ImDrawList* draw, const ImVec2& at, double headingDegrees,
                      double ageSeconds) {
    const float radians = static_cast<float>(headingDegrees * 3.14159265358979 / 180.0);
    const float sin = std::sin(radians);
    const float cos = std::cos(radians);

    // Screen Y grows downward while a bearing of 0 points up, so the forward
    // vector is (sin, -cos).
    const ImVec2 forward(sin, -cos);
    const ImVec2 right(cos, sin);

    const float length = 15.0f;
    const float halfWidth = 8.0f;

    const ImVec2 tip(at.x + forward.x * length, at.y + forward.y * length);
    const ImVec2 left(at.x - forward.x * length * 0.55f - right.x * halfWidth,
                      at.y - forward.y * length * 0.55f - right.y * halfWidth);
    const ImVec2 rightPoint(at.x - forward.x * length * 0.55f + right.x * halfWidth,
                            at.y - forward.y * length * 0.55f + right.y * halfWidth);

    const ImU32 colour = markerColour(ageSeconds);
    draw->AddTriangleFilled(tip, left, rightPoint, colour);
    draw->AddTriangle(tip, left, rightPoint, rgba(0.05f, 0.06f, 0.05f, 0.9f), 2.0f);

    // A ring that grows with the age of the fix: a visual statement of how far
    // the player could plausibly have walked since we last knew where they were.
    // Roughly 5 m/s of sprint, converted to screen units by the caller's scale
    // is not available here, so this is a fixed cue rather than a true radius.
    if (ageSeconds > 20.0) {
        const float radius =
            18.0f + static_cast<float>(std::min(ageSeconds, 120.0)) * 0.35f;
        draw->AddCircle(at, radius, markerColour(ageSeconds), 0, 1.5f);
    }
}

}  // namespace

MapViewResult drawMapView(MapViewState& state, const geo::MapVariant& map,
                          const game::RaidSnapshot& snapshot,
                          std::int64_t nowMillis, bool interactive,
                          const TileTextureProvider& tiles) {
    MapViewResult result;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowBgAlpha(0.0f);

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

    if (!ImGui::Begin("tarkov-help-map", nullptr, flags)) {
        ImGui::End();
        return result;
    }

    const ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    const int width = static_cast<int>(canvasSize.x);
    const int height = static_cast<int>(canvasSize.y);

    const bool mapChanged = state.mapName != map.key;
    if (!state.initialised || mapChanged ||
        state.viewport.screenWidth != width || state.viewport.screenHeight != height) {
        if (!state.initialised || mapChanged) {
            state.viewport = map::fitToBounds(map, width, height);
            state.mapName = map.key;
            state.followPlayer = true;
            state.initialised = true;
        } else {
            state.viewport.screenWidth = width;
            state.viewport.screenHeight = height;
        }
        result.wantsRedraw = true;
    }

    const geo::Projection projection(map);

    // Re-centre on a new fix while following. Compare the fix time rather than
    // the position: standing still is not a reason to stop following.
    static std::int64_t lastFollowedFix = -1;
    if (state.followPlayer && snapshot.fix.valid &&
        snapshot.fix.receivedAtMillis != lastFollowedFix) {
        lastFollowedFix = snapshot.fix.receivedAtMillis;
        state.viewport.centerBase = projection.toImage(snapshot.fix.world);
        result.wantsRedraw = true;
    }

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();

    // A dim backdrop so map tiles read against a bright game scene.
    draw->AddRectFilled(origin,
                        ImVec2(origin.x + canvasSize.x, origin.y + canvasSize.y),
                        rgba(0.03f, 0.04f, 0.04f, 0.90f));

    const int zoom = map::chooseZoom(map, state.viewport.scale);
    const auto visible = map::visibleTiles(map, state.viewport, zoom);

    for (const auto& tile : visible) {
        const ImVec2 topLeft(origin.x + static_cast<float>(tile.topLeft.x),
                             origin.y + static_cast<float>(tile.topLeft.y));
        const ImVec2 bottomRight(origin.x + static_cast<float>(tile.bottomRight.x),
                                 origin.y + static_cast<float>(tile.bottomRight.y));

        const ImTextureID texture = tiles ? tiles(tile.zoom, tile.x, tile.y) : 0;
        if (texture != 0) {
            draw->AddImage(texture, topLeft, bottomRight);
            ++result.tilesDrawn;
        } else {
            // A placeholder rather than a hole: an empty rectangle reads as
            // "loading", while nothing at all reads as "broken".
            draw->AddRectFilled(topLeft, bottomRight, rgba(0.09f, 0.10f, 0.10f, 0.55f));
            ++result.tilesRequested;
        }
    }

    // The map's own extent, so the player can see where the data stops.
    if (const auto bounds = projection.imageBounds()) {
        const auto a = map::baseToScreen(state.viewport, bounds->first);
        const auto b = map::baseToScreen(state.viewport, bounds->second);
        draw->AddRect(ImVec2(origin.x + static_cast<float>(a.x),
                             origin.y + static_cast<float>(a.y)),
                      ImVec2(origin.x + static_cast<float>(b.x),
                             origin.y + static_cast<float>(b.y)),
                      // ImGui 1.92.8 swapped thickness and flags; this is
                      // (rounding, thickness).
                      rgba(0.45f, 0.50f, 0.42f, 0.55f), 0.0f, 1.5f);
    }

    if (snapshot.fix.valid) {
        const auto base = projection.toImage(snapshot.fix.world);
        const auto screen = map::baseToScreen(state.viewport, base);
        const ImVec2 at(origin.x + static_cast<float>(screen.x),
                        origin.y + static_cast<float>(screen.y));

        const double heading = projection.toImageHeading(snapshot.fix.headingDegrees);
        drawPlayerMarker(draw, at, heading, snapshot.fix.ageSeconds(nowMillis));
    }

    // Interaction. Only while the overlay actually receives input; with
    // click-through on, ImGui sees nothing and must not pretend otherwise.
    if (interactive) {
        ImGui::InvisibleButton("map-canvas", canvasSize,
                               ImGuiButtonFlags_MouseButtonLeft |
                                   ImGuiButtonFlags_MouseButtonRight);
        const ImGuiIO& io = ImGui::GetIO();

        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            state.viewport.centerBase.x -= io.MouseDelta.x / state.viewport.scale;
            state.viewport.centerBase.y -= io.MouseDelta.y / state.viewport.scale;
            state.followPlayer = false;  // Dragging means "I want to look elsewhere".
            state.viewport = map::clampToBounds(map, state.viewport);
            result.wantsRedraw = true;
        }

        if (ImGui::IsItemHovered() && io.MouseWheel != 0.0f) {
            const geo::Vec2 anchor{io.MousePos.x - origin.x, io.MousePos.y - origin.y};
            const double factor = std::pow(1.2, io.MouseWheel);
            state.viewport =
                map::zoomAbout(state.viewport, anchor, factor, kMinScale, kMaxScale);
            result.wantsRedraw = true;
        }
    }

    // Footer: the state of the view, and how to get back to the player.
    {
        char text[160];
        const char* mode = state.followPlayer ? "following" : "free";
        std::snprintf(text, sizeof(text), "%s   zoom %d   scale %.2f   %s",
                      map.key.c_str(), zoom, state.viewport.scale, mode);

        const ImVec2 size = ImGui::CalcTextSize(text);
        const ImVec2 at(origin.x + 16.0f, origin.y + canvasSize.y - size.y - 16.0f);
        draw->AddRectFilled(ImVec2(at.x - 8.0f, at.y - 6.0f),
                            ImVec2(at.x + size.x + 8.0f, at.y + size.y + 6.0f),
                            rgba(0.04f, 0.05f, 0.05f, 0.80f), 4.0f);
        draw->AddText(at, rgba(0.70f, 0.73f, 0.66f, 1.0f), text);
    }

    ImGui::End();
    return result;
}

}  // namespace tarkov::ui
