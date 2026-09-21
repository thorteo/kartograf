#include "ui/MapView.h"

#include <string>
#include <vector>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core/geo/Heading.h"
#include "core/geo/Projection.h"

namespace tarkov::ui {
namespace {

constexpr float kMinOpacity = 0.15f;
constexpr float kMaxOpacity = 1.00f;

constexpr double kMinScale = 0.05;
constexpr double kMaxScale = 24.0;

/// Zoom used the first time the view has a position to centre on. Chosen to
/// show a few hundred metres around the player, which is the range that matters
/// while moving.
constexpr double kDefaultPlayerScale = 3.0;

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

void drawQuestMarker(ImDrawList* draw, const ImVec2& at, bool questItem,
                     bool optional, bool onLevel) {
    const float alpha = onLevel ? 1.0f : 0.35f;
    const ImU32 fill = optional ? rgba(0.62f, 0.68f, 0.82f, alpha)
                                : rgba(0.96f, 0.82f, 0.36f, alpha);
    const ImU32 edge = rgba(0.05f, 0.06f, 0.05f, alpha * 0.9f);

    if (questItem) {
        // A diamond: a possible spawn, not a fixed destination.
        const float r = 7.0f;
        const ImVec2 points[4] = {{at.x, at.y - r},
                                  {at.x + r, at.y},
                                  {at.x, at.y + r},
                                  {at.x - r, at.y}};
        draw->AddConvexPolyFilled(points, 4, fill);
        draw->AddPolyline(points, 4, edge, 1.5f, ImDrawFlags_Closed);
    } else {
        draw->AddCircleFilled(at, 6.0f, fill);
        draw->AddCircle(at, 6.0f, edge, 0, 1.5f);
    }
}

}  // namespace

bool QuestPlace::onSameLevelAs(double elevation) const {
    if (!top && !bottom) {
        return true;
    }
    if (bottom && elevation < *bottom) {
        return false;
    }
    if (top && elevation > *top) {
        return false;
    }
    return true;
}

std::string formatFixAge(double seconds) {
    if (seconds < 0.0) {
        seconds = 0.0;
    }
    const int total = static_cast<int>(seconds);
    if (total < 60) {
        return std::to_string(total) + "s";
    }
    return std::to_string(total / 60) + "m" + std::to_string(total % 60) + "s";
}

std::string layerName(const geo::MapVariant& map, int layer) {
    if (layer < 0) {
        return "Ground";
    }
    if (static_cast<std::size_t>(layer) >= map.layers.size()) {
        return "Layer " + std::to_string(layer);
    }
    const std::string& name = map.layers[static_cast<std::size_t>(layer)].name;
    return name.empty() ? ("Layer " + std::to_string(layer)) : name;
}

MapViewResult drawMapView(MapViewState& state, const geo::MapVariant& map,
                          const game::RaidSnapshot& snapshot,
                          std::int64_t nowMillis, bool interactive,
                          const TileTextureProvider& tiles,
                          const std::vector<QuestPlace>& quests) {
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

    // The overlay style gives panels a border, which is right for the HUD but
    // draws a frame around the whole monitor when the window is fullscreen.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const bool open = ImGui::Begin("tarkov-help-map", nullptr, flags);
    if (!open) {
        ImGui::End();
        ImGui::PopStyleVar(2);
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
        state.viewport = map::clampToBounds(map, state.viewport);
        result.wantsRedraw = true;
    }

    const geo::Projection projection(map);

    // Re-centre on a new fix while following. Compare the fix time rather than
    // the position: standing still is not a reason to stop following.
    if (state.followPlayer && snapshot.fix.valid &&
        snapshot.fix.receivedAtMillis != state.lastFollowedFix) {
        const bool first = state.lastFollowedFix == 0;
        state.lastFollowedFix = snapshot.fix.receivedAtMillis;
        state.viewport.centerBase = projection.toImage(snapshot.fix.world);

        // Opening the map in a raid should show the surroundings, not the whole
        // location, and it should fill the screen. Fitting the map letterboxes
        // whenever the aspect ratios differ, and empty margins around a map
        // centred on the player read as a broken map rather than as the edge of
        // the world.
        if (first) {
            const double cover =
                map::coverScale(map, state.viewport.screenWidth,
                                state.viewport.screenHeight);
            state.viewport.scale =
                std::max({state.viewport.scale, cover, kDefaultPlayerScale});
        }
        // Following the player must not drag the map off the screen edge any
        // more than dragging by hand may.
        state.viewport = map::clampToBounds(map, state.viewport);
        result.wantsRedraw = true;
    }

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();

    state.opacity = std::clamp(state.opacity, kMinOpacity, kMaxOpacity);

    // A dim backdrop so map tiles read against a bright game scene. It fades
    // with the map, otherwise turning the map down would leave the scene behind
    // it just as obscured as before.
    draw->AddRectFilled(origin,
                        ImVec2(origin.x + canvasSize.x, origin.y + canvasSize.y),
                        rgba(0.03f, 0.04f, 0.04f, 0.88f * state.opacity));

    // Tiles carry the same alpha, so the game shows through the map itself
    // rather than only around it.
    const ImU32 tileTint =
        ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, state.opacity));

    // Which floor to show. Following the player's elevation is the point of
    // the feature: walking upstairs in Dorms should change the map without the
    // player reaching for anything.
    if (state.layerAuto) {
        const int wanted =
            snapshot.fix.valid
                ? static_cast<int>(
                      map::layerIndexOrGround(map, snapshot.fix.world))
                : -1;
        if (wanted != state.activeLayer) {
            state.activeLayer = wanted;
            result.wantsRedraw = true;
        }
    }
    // Guard against a stale index after a map or data change.
    if (state.activeLayer >= static_cast<int>(map.layers.size())) {
        state.activeLayer = -1;
    }

    // Three maps publish no raster tiles at all -- Streets, Lighthouse and
    // Terminal are SVG-only upstream, confirmed by probing the CDN. Drawing a
    // grid of empty placeholders for them would look like a broken download
    // rather than missing data, so say what is actually wrong.
    if (map.tilePath.empty()) {
        const ImVec2 centre(origin.x + canvasSize.x / 2.0f,
                            origin.y + canvasSize.y / 2.0f);
        const char* headline = "No map image available for this location yet.";
        const char* detail =
            "This map is published as vector art only. Position tracking still "
            "works; the HUD has your coordinates.";
        const ImVec2 headlineSize = ImGui::CalcTextSize(headline);
        const ImVec2 detailSize = ImGui::CalcTextSize(detail);
        draw->AddText(ImVec2(centre.x - headlineSize.x / 2.0f, centre.y - 16.0f),
                      rgba(0.86f, 0.87f, 0.83f, 1.0f), headline);
        draw->AddText(ImVec2(centre.x - detailSize.x / 2.0f, centre.y + 8.0f),
                      rgba(0.58f, 0.60f, 0.55f, 1.0f), detail);
        ImGui::End();
        ImGui::PopStyleVar(2);
        return result;
    }

    const int zoom = map::chooseZoom(map, state.viewport.scale);
    const auto visible = map::visibleTiles(map, state.viewport, zoom);

    // Ground first, then the active floor over it. Upper-floor tiles cover only
    // the buildings that have one, so the ground underneath has to stay drawn
    // or the rest of the map would disappear when the player goes inside.
    std::vector<int> passes{-1};
    const bool activeLayerDrawable =
        state.activeLayer >= 0 &&
        static_cast<std::size_t>(state.activeLayer) < map.layers.size() &&
        !map.layers[static_cast<std::size_t>(state.activeLayer)].tilePath.empty();
    if (activeLayerDrawable) {
        passes.push_back(state.activeLayer);
    }

    for (const int layer : passes) {
        for (const auto& tile : visible) {
            const ImVec2 topLeft(origin.x + static_cast<float>(tile.topLeft.x),
                                 origin.y + static_cast<float>(tile.topLeft.y));
            const ImVec2 bottomRight(
                origin.x + static_cast<float>(tile.bottomRight.x),
                origin.y + static_cast<float>(tile.bottomRight.y));

            const ImTextureID texture =
                tiles ? tiles(layer, tile.zoom, tile.x, tile.y) : 0;
            if (texture != 0) {
                draw->AddImage(texture, topLeft, bottomRight, ImVec2(0.0f, 0.0f),
                               ImVec2(1.0f, 1.0f), tileTint);
                ++result.tilesDrawn;
            } else if (layer == -1) {
                // A placeholder rather than a hole: an empty rectangle reads as
                // "loading", while nothing at all reads as "broken". Only for
                // ground -- a missing upper-floor tile usually means that floor
                // simply does not cover this spot.
                draw->AddRectFilled(
                    topLeft, bottomRight,
                    rgba(0.09f, 0.10f, 0.10f, 0.55f * state.opacity));
                ++result.tilesRequested;
            }
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

    // Quest markers, under the player so the player is never hidden by one.
    if (state.showQuests && !quests.empty()) {
        const double playerElevation =
            snapshot.fix.valid ? snapshot.fix.world.y : 0.0;
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const QuestPlace* hovered = nullptr;
        ImVec2 hoveredAt{};

        for (const auto& place : quests) {
            const auto screen =
                map::baseToScreen(state.viewport, projection.toImage(place.world));
            if (screen.x < -32.0 || screen.y < -32.0 ||
                screen.x > state.viewport.screenWidth + 32.0 ||
                screen.y > state.viewport.screenHeight + 32.0) {
                continue;  // Off screen; nothing to draw or hit-test.
            }
            const ImVec2 at(origin.x + static_cast<float>(screen.x),
                            origin.y + static_cast<float>(screen.y));

            const bool onLevel = !state.dimOtherFloors || !snapshot.fix.valid ||
                                 place.onSameLevelAs(playerElevation);
            drawQuestMarker(draw, at, place.isQuestItem, place.optional, onLevel);

            if (interactive) {
                const float dx = mouse.x - at.x;
                const float dy = mouse.y - at.y;
                if (dx * dx + dy * dy < 12.0f * 12.0f) {
                    hovered = &place;
                    hoveredAt = at;
                }
            }
        }

        if (hovered != nullptr) {
            ImGui::SetNextWindowPos(ImVec2(hoveredAt.x + 14.0f, hoveredAt.y + 14.0f));
            ImGui::SetNextWindowBgAlpha(0.92f);
            if (ImGui::Begin("tarkov-help-quest-tip", nullptr,
                             ImGuiWindowFlags_NoDecoration |
                                 ImGuiWindowFlags_AlwaysAutoResize |
                                 ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_NoNav |
                                 ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoFocusOnAppearing)) {
                ImGui::TextUnformatted(hovered->taskName.c_str());
                if (!hovered->detail.empty()) {
                    ImGui::PushTextWrapPos(420.0f);
                    ImGui::TextColored(ImVec4(0.72f, 0.74f, 0.68f, 1.0f), "%s",
                                       hovered->detail.c_str());
                    ImGui::PopTextWrapPos();
                }
                if (hovered->optional) {
                    ImGui::TextColored(ImVec4(0.62f, 0.68f, 0.82f, 1.0f), "optional");
                }
                if (hovered->isQuestItem) {
                    ImGui::TextColored(ImVec4(0.72f, 0.74f, 0.68f, 1.0f),
                                       "one of several possible spawns");
                }
            }
            ImGui::End();
        }
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

        if (ImGui::IsItemHovered() && io.MouseWheel != 0.0f && io.KeyCtrl) {
            // Ctrl+wheel adjusts how much of the game shows through, which is
            // the thing most worth reaching for mid-raid.
            state.opacity = std::clamp(state.opacity + io.MouseWheel * 0.05f,
                                       kMinOpacity, kMaxOpacity);
            result.wantsRedraw = true;
        } else if (ImGui::IsItemHovered() && io.MouseWheel != 0.0f) {
            const geo::Vec2 anchor{io.MousePos.x - origin.x, io.MousePos.y - origin.y};
            const double factor = std::pow(1.2, io.MouseWheel);
            state.viewport =
                map::zoomAbout(state.viewport, anchor, factor, kMinScale, kMaxScale);
            state.viewport = map::clampToBounds(map, state.viewport);
            result.wantsRedraw = true;
        }
    }

    // Floor selector, only while the overlay takes input.
    if (interactive) {
        ImGui::SetNextWindowPos(ImVec2(origin.x + 16.0f, origin.y + 16.0f),
                                ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.85f);
        if (ImGui::Begin("tarkov-help-floors", nullptr,
                         ImGuiWindowFlags_NoDecoration |
                             ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove)) {
            ImGui::SetNextItemWidth(130.0f);
            if (ImGui::SliderFloat("##opacity", &state.opacity, kMinOpacity,
                                   kMaxOpacity, "opacity %.2f")) {
                result.wantsRedraw = true;
            }
            if (map.layers.empty()) {
                ImGui::TextDisabled("no upper floors");
            } else {
                ImGui::Separator();
                if (ImGui::Checkbox("auto", &state.layerAuto)) {
                    result.wantsRedraw = true;
                }
            }
            // Listed top-down, the way a building reads.
            for (int index = static_cast<int>(map.layers.size()) - 1;
                 !map.layers.empty() && index >= -1; --index) {
                const bool selected = state.activeLayer == index;
                if (ImGui::Selectable(layerName(map, index).c_str(), selected)) {
                    state.activeLayer = index;
                    state.layerAuto = false;
                    result.wantsRedraw = true;
                }
            }
        }
        ImGui::End();
    }

    // Footer: the state of the view, and how to get back to the player.
    {
        char text[340];
        const char* mode = state.followPlayer ? "following" : "free";
        // Interchange and Shoreline declare upper floors with no tiles. Saying
        // so beats silently showing the ground level and letting the player
        // believe they are looking at the floor they are standing on.
        const bool floorMissing = state.activeLayer >= 0 && !activeLayerDrawable;

        // The age of the fix is the one thing the map cannot show by itself and
        // the game does not show at all. It decides whether the marker means
        // anything, so it belongs where the marker is.
        char age[48];
        if (snapshot.fix.valid) {
            const double seconds = snapshot.fix.ageSeconds(nowMillis);
            std::snprintf(age, sizeof(age), "   fix %s%s",
                          formatFixAge(seconds).c_str(),
                          seconds >= 60.0 ? " - press screenshot" : "");
        } else {
            std::snprintf(age, sizeof(age), "   no position - press screenshot");
        }

        char questText[48] = "";
        if (!quests.empty()) {
            std::snprintf(questText, sizeof(questText), "   %d objectives",
                          static_cast<int>(quests.size()));
        }
        std::snprintf(text, sizeof(text), "%s   %s%s%s   %s%s%s", map.key.c_str(),
                      layerName(map, state.activeLayer).c_str(),
                      state.layerAuto ? " (auto)" : "",
                      floorMissing ? " - no image for this floor" : "", mode, age,
                      questText);

        const ImVec2 size = ImGui::CalcTextSize(text);
        // Bottom centre: the status log lives bottom-left and the HUD
        // top-right, and a footer underneath either of them is unreadable.
        const ImVec2 at(origin.x + (canvasSize.x - size.x) / 2.0f,
                        origin.y + canvasSize.y - size.y - 16.0f);
        draw->AddRectFilled(ImVec2(at.x - 8.0f, at.y - 6.0f),
                            ImVec2(at.x + size.x + 8.0f, at.y + size.y + 6.0f),
                            rgba(0.04f, 0.05f, 0.05f, 0.80f), 4.0f);
        const ImU32 footerColour =
            snapshot.fix.valid
                ? markerColour(snapshot.fix.ageSeconds(nowMillis))
                : rgba(0.70f, 0.73f, 0.66f, 1.0f);
        draw->AddText(at, footerColour, text);
    }

    ImGui::End();
    ImGui::PopStyleVar(2);
    return result;
}

}  // namespace tarkov::ui
