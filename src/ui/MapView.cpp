#include "ui/MapView.h"

#include <string>
#include <vector>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core/geo/Heading.h"
#include "core/geo/Projection.h"
#include "ui/Strings.h"

namespace tarkov::ui {
namespace {

constexpr float kMinOpacity = 0.15f;
constexpr float kMaxOpacity = 1.00f;

// Marker and label sizes, in screen pixels: they do not scale with the map, so
// they stay legible when a large map is fitted to the screen.
constexpr float kLabelFontSize = 17.0f;
constexpr float kQuestSize = 28.0f;
constexpr float kOptionalQuestSize = 21.0f;
constexpr float kExtractSize = 30.0f;
constexpr float kSpawnSize = 20.0f;
/// How close the pointer must be to a marker's centre to explain it.
constexpr float kHoverRadius = 18.0f;

constexpr double kMinScale = 0.05;
constexpr double kMaxScale = 24.0;

/// How much of the screen the fitted map takes. All of it: the whole map in
/// view and as large as it will go. Margins are the game showing through, not
/// wasted space.
constexpr double kFitPadding = 1.0;

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

/// How long one pulse takes, and how many follow a new fix or the map opening.
///
/// Bounded on purpose: rendering is reactive, and a pulse that never stopped
/// would put the overlay back to presenting every frame -- measured at 9.8% of
/// a core against 0.2% idle. A few seconds is enough to catch the eye.
constexpr std::int64_t kPulsePeriodMillis = 1200;
constexpr int kPulseCount = 3;

/// Where in its pulse the marker is, 0..1, or a negative value when it is not
/// pulsing.
float pulsePhase(std::int64_t pulseFrom, std::int64_t nowMillis) {
    if (pulseFrom == 0 || nowMillis < pulseFrom) {
        return -1.0f;
    }
    const std::int64_t elapsed = nowMillis - pulseFrom;
    if (elapsed >= kPulsePeriodMillis * kPulseCount) {
        return -1.0f;
    }
    return static_cast<float>(elapsed % kPulsePeriodMillis) /
           static_cast<float>(kPulsePeriodMillis);
}

/// The player: a dot with a pointer and a view cone toward the heading.
///
/// Drawn rather than taken from an icon so it stays crisp at any size and can
/// carry the fix's age in its colour. The dark rim keeps it readable on the
/// pale parts of the map, the white one on the dark parts.
void drawPlayerMarker(ImDrawList* draw, const ImVec2& at, double headingDegrees,
                      double ageSeconds, float pulse) {
    const float radians = static_cast<float>(headingDegrees * 3.14159265358979 / 180.0);
    // Screen Y grows downward while a bearing of 0 points up, so the forward
    // vector is (sin, -cos).
    const ImVec2 forward(std::sin(radians), -std::cos(radians));
    const ImVec2 right(-forward.y, forward.x);
    const auto along = [&](float ahead, float aside) {
        return ImVec2(at.x + forward.x * ahead + right.x * aside,
                      at.y + forward.y * ahead + right.y * aside);
    };

    const bool stale = ageSeconds >= 60.0;
    const float alpha = stale ? 0.6f : 1.0f;
    const ImVec4 tint = ImGui::ColorConvertU32ToFloat4(markerColour(ageSeconds));
    const auto colour = [&](float a) { return rgba(tint.x, tint.y, tint.z, a); };

    // An expanding ring, fading as it grows.
    if (pulse >= 0.0f) {
        const float eased = 1.0f - (1.0f - pulse) * (1.0f - pulse);
        const float fade = (1.0f - pulse) * (1.0f - pulse);
        draw->AddCircle(at, 10.0f + eased * 30.0f, colour(0.85f * fade * alpha), 0,
                        2.5f);
    }

    // The view cone: three nested wedges stand in for a radial gradient, which
    // ImDrawList cannot fill.
    constexpr float kHalfAngle = 0.55f;  // about 32 degrees either side
    const float heading = std::atan2(forward.y, forward.x);
    for (const float reach : {52.0f, 38.0f, 24.0f}) {
        draw->PathLineTo(at);
        draw->PathArcTo(at, reach, heading - kHalfAngle, heading + kHalfAngle, 12);
        draw->PathFillConvex(colour(0.13f * alpha));
    }

    // The pointer, on the rim of the dot.
    const ImVec2 tip = along(17.0f, 0.0f);
    const ImVec2 baseLeft = along(7.0f, -6.5f);
    const ImVec2 baseRight = along(7.0f, 6.5f);
    draw->AddTriangleFilled(tip, baseLeft, baseRight, rgba(0.04f, 0.05f, 0.04f, 0.55f * alpha));
    draw->AddTriangleFilled(along(15.5f, 0.0f), along(7.5f, -5.0f), along(7.5f, 5.0f),
                            rgba(1.0f, 1.0f, 1.0f, alpha));

    // The dot: dark rim, white ring, coloured core.
    draw->AddCircleFilled(at, 10.5f, rgba(0.04f, 0.05f, 0.04f, 0.55f * alpha), 24);
    draw->AddCircleFilled(at, 9.0f, rgba(1.0f, 1.0f, 1.0f, alpha), 24);
    draw->AddCircleFilled(at, 6.5f, colour(alpha), 24);

    // A ring that grows with the age of the fix: roughly how far the player
    // could have walked since we last knew where they were. A fixed cue, not a
    // true radius -- the scale is not known here.
    if (ageSeconds > 20.0) {
        const float radius =
            18.0f + static_cast<float>(std::min(ageSeconds, 120.0)) * 0.35f;
        draw->AddCircle(at, radius, colour(alpha * 0.8f), 0, 1.5f);
    }
}

ImU32 fallbackColour(MarkerIcon icon, float alpha) {
    switch (icon) {
        case MarkerIcon::QuestObjective: return rgba(0.96f, 0.82f, 0.36f, alpha);
        case MarkerIcon::ExtractPmc: return rgba(0.25f, 0.85f, 0.55f, alpha);
        case MarkerIcon::ExtractScav: return rgba(0.95f, 0.62f, 0.20f, alpha);
        case MarkerIcon::ExtractShared: return rgba(0.25f, 0.85f, 0.85f, alpha);
        case MarkerIcon::Transit: return rgba(0.90f, 0.30f, 0.25f, alpha);
        case MarkerIcon::SpawnPmc: return rgba(0.95f, 0.90f, 0.30f, alpha);
        case MarkerIcon::SpawnBoss: return rgba(0.95f, 0.95f, 0.95f, alpha);
    }
    return rgba(1.0f, 1.0f, 1.0f, alpha);
}

/// Draws an icon centred on `at`, or a vector stand-in of the same colour if
/// the icon has not been downloaded (or cannot be: offline first run).
void drawIcon(ImDrawList* draw, const IconProvider& icons, MarkerIcon icon,
              const ImVec2& at, float size, float alpha) {
    const ImTextureID texture = icons ? icons(icon) : 0;
    const float half = size / 2.0f;
    if (texture != 0) {
        draw->AddImage(texture, ImVec2(at.x - half, at.y - half),
                       ImVec2(at.x + half, at.y + half), ImVec2(0.0f, 0.0f),
                       ImVec2(1.0f, 1.0f), rgba(1.0f, 1.0f, 1.0f, alpha));
        return;
    }
    const ImU32 fill = fallbackColour(icon, alpha);
    const ImU32 edge = rgba(0.05f, 0.06f, 0.05f, alpha * 0.9f);
    const float r = half * 0.6f;
    draw->AddCircleFilled(at, r, fill);
    draw->AddCircle(at, r, edge, 0, 1.5f);
}

/// The colour that says what an objective asks for.
ImVec4 objectiveColour(data::ObjectiveKind kind, float alpha) {
    switch (kind) {
        case data::ObjectiveKind::Visit: return ImVec4(0.30f, 0.64f, 1.00f, alpha);
        case data::ObjectiveKind::Mark: return ImVec4(1.00f, 0.54f, 0.16f, alpha);
        case data::ObjectiveKind::Plant: return ImVec4(0.69f, 0.49f, 1.00f, alpha);
        case data::ObjectiveKind::Find: return ImVec4(0.95f, 0.79f, 0.30f, alpha);
        case data::ObjectiveKind::Shoot: return ImVec4(0.92f, 0.34f, 0.34f, alpha);
        case data::ObjectiveKind::Use: return ImVec4(0.26f, 0.77f, 0.54f, alpha);
        case data::ObjectiveKind::Other: break;
    }
    return ImVec4(0.96f, 0.82f, 0.36f, alpha);
}

/// A quest marker: a disc in the colour of what to do there, with a white ring
/// and a white glyph -- a magnifier to find, a radio tower to mark. Colour and
/// glyph tell the objectives apart; the white ring holds the disc against any
/// part of the map.
void drawQuestMarker(ImDrawList* draw, const MapOverlays& overlays,
                     const QuestPlace& place, const ImVec2& at, float size, float alpha) {
    if (place.kind == data::ObjectiveKind::Other) {
        drawIcon(draw, overlays.icons, MarkerIcon::QuestObjective, at, size, alpha);
        return;
    }
    const ImTextureID glyph = overlays.glyphs ? overlays.glyphs(place.kind) : 0;
    const ImU32 colour = ImGui::GetColorU32(objectiveColour(place.kind, alpha));
    const ImU32 white = rgba(1.0f, 1.0f, 1.0f, alpha);
    const float r = size / 2.0f;
    draw->AddCircleFilled(at, r + 3.0f, rgba(0.03f, 0.04f, 0.03f, 0.6f * alpha), 24);
    draw->AddCircleFilled(at, r + 1.8f, white, 24);
    draw->AddCircleFilled(at, r, colour, 24);
    if (glyph != 0) {
        const float g = r * 0.6f;
        draw->AddImage(glyph, ImVec2(at.x - g, at.y - g), ImVec2(at.x + g, at.y + g),
                       ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), white);
    } else {
        // Until the glyph is rasterised, which is a frame or two at startup.
        draw->AddCircleFilled(at, r * 0.3f, white, 12);
    }
}

}  // namespace

std::optional<ScreenRect> placeMarkerLabel(const ImVec2& anchor, float iconHalf,
                                           const ImVec2& boxSize, const ScreenRect& screen,
                                           const std::vector<ScreenRect>& placed) {
    constexpr float kGap = 3.0f;
    const auto overlaps = [&](const ScreenRect& box) {
        for (const auto& other : placed) {
            if (box.min.x < other.max.x && box.max.x > other.min.x &&
                box.min.y < other.max.y && box.max.y > other.min.y) {
                return true;
            }
        }
        return false;
    };
    const auto at = [&](float left) {
        // Kept inside the screen: an exit on the very edge of the map is on
        // the very edge of the screen once the map is fitted.
        left = std::clamp(left, screen.min.x, std::max(screen.min.x, screen.max.x - boxSize.x));
        float top = anchor.y - boxSize.y / 2.0f;
        top = std::clamp(top, screen.min.y, std::max(screen.min.y, screen.max.y - boxSize.y));
        return ScreenRect{ImVec2(left, top), ImVec2(left + boxSize.x, top + boxSize.y)};
    };

    const float rightOf = anchor.x + iconHalf + kGap;
    const float leftOf = anchor.x - iconHalf - kGap - boxSize.x;
    const bool fitsRight = rightOf + boxSize.x <= screen.max.x;
    const bool fitsLeft = leftOf >= screen.min.x;

    // Beside the icon on a side with room, right first; then pushed inside
    // the screen, which may cover the icon but not cut the name.
    for (const auto& candidate : {fitsRight ? std::optional<float>(rightOf) : std::nullopt,
                                  fitsLeft ? std::optional<float>(leftOf) : std::nullopt,
                                  std::optional<float>(fitsRight || !fitsLeft ? rightOf
                                                                              : leftOf)}) {
        if (!candidate) {
            continue;
        }
        const ScreenRect box = at(*candidate);
        if (!overlaps(box)) {
            return box;
        }
    }
    return std::nullopt;
}

namespace {

/// How specific an objective is about what to do at its place; the merged
/// marker takes the highest.
int specificity(data::ObjectiveKind kind) {
    switch (kind) {
        case data::ObjectiveKind::Mark: return 6;
        case data::ObjectiveKind::Plant: return 5;
        case data::ObjectiveKind::Use: return 4;
        case data::ObjectiveKind::Shoot: return 3;
        case data::ObjectiveKind::Find: return 2;
        case data::ObjectiveKind::Visit: return 1;
        case data::ObjectiveKind::Other: return 0;
    }
    return 0;
}

}  // namespace

std::vector<QuestPlace> mergeNearbyQuestPlaces(std::vector<QuestPlace> places) {
    // Horizontal and vertical reach, in metres. Wide enough for "locate the
    // room" and "mark the room" set a few metres apart; the height limit keeps
    // an objective on the floor above from being folded into this one.
    constexpr double kReach = 10.0;
    constexpr double kHeight = 3.0;

    std::vector<QuestPlace> merged;
    merged.reserve(places.size());
    for (auto& place : places) {
        QuestPlace* into = nullptr;
        if (!place.isQuestItem) {
            for (auto& existing : merged) {
                const double dx = existing.world.x - place.world.x;
                const double dz = existing.world.z - place.world.z;
                if (!existing.isQuestItem && existing.taskName == place.taskName &&
                    dx * dx + dz * dz <= kReach * kReach &&
                    std::abs(existing.world.y - place.world.y) <= kHeight) {
                    into = &existing;
                    break;
                }
            }
        }
        if (into == nullptr) {
            merged.push_back(std::move(place));
            continue;
        }
        if (specificity(place.kind) > specificity(into->kind)) {
            into->kind = place.kind;
        }
        // Required if any part is required.
        into->optional = into->optional && place.optional;
        if (!place.detail.empty() && into->detail.find(place.detail) == std::string::npos) {
            if (!into->detail.empty()) {
                into->detail += "\n";
            }
            into->detail += place.detail;
        }
    }
    return merged;
}

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
        return trf(Str::AgeSeconds, total);
    }
    return trf(Str::AgeMinutesSeconds, total / 60, total % 60);
}

std::string layerName(const geo::MapVariant& map, int layer) {
    if (layer < 0) {
        return tr(Str::Ground);
    }
    if (static_cast<std::size_t>(layer) >= map.layers.size()) {
        return trf(Str::LayerN, layer);
    }
    const std::string& name = map.layers[static_cast<std::size_t>(layer)].name;
    return name.empty() ? trf(Str::LayerN, layer) : floorDisplayName(name);
}

MapViewResult drawMapView(MapViewState& state, const geo::MapVariant& map,
                          const game::RaidSnapshot& snapshot,
                          std::int64_t nowMillis, bool interactive,
                          const TileTextureProvider& tiles,
                          const MapOverlays& overlays,
                          const AbstractMapProvider& abstractMap) {
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
            state.viewport = map::fitToBounds(map, width, height, kFitPadding);
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

    // Draw the eye to a new position, whether or not the view follows it.
    if (snapshot.fix.valid && snapshot.fix.receivedAtMillis != state.lastPulsedFix) {
        state.lastPulsedFix = snapshot.fix.receivedAtMillis;
        state.pulseFromMillis = nowMillis;
    }

    // Re-centre on a new fix while following. Compare the fix time rather than
    // the position: standing still is not a reason to stop following.
    if (state.followPlayer && snapshot.fix.valid &&
        snapshot.fix.receivedAtMillis != state.lastFollowedFix) {
        state.lastFollowedFix = snapshot.fix.receivedAtMillis;
        state.viewport.centerBase = projection.toImage(snapshot.fix.world);

        // No zoom: the fitted map already shows everything as large as the
        // screen allows. Zooming in to cover the screen used to cut off up to a
        // sixth of a wide map like Customs, out of reach without Alt+N. Only a
        // view the user zoomed into by hand actually moves here.
        //
        // Following the player must not drag the map off the screen edge any
        // more than dragging by hand may; on an axis where the map fits, this
        // keeps it centred.
        state.viewport = map::clampToBounds(map, state.viewport);
        result.wantsRedraw = true;
    }

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();

    state.opacity = std::clamp(state.opacity, kMinOpacity, kMaxOpacity);

    // Tiles and the abstract drawing carry the same alpha, so the game shows
    // through the map itself rather than only around it.
    const ImU32 tileTint =
        ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, state.opacity));

    // A dim backdrop so map tiles read against a bright game scene. It fades
    // with the map, otherwise turning the map down would leave the scene behind
    // it just as obscured as before.
    draw->AddRectFilled(origin,
                        ImVec2(origin.x + canvasSize.x, origin.y + canvasSize.y),
                        rgba(0.03f, 0.04f, 0.04f, 0.88f * state.opacity));

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

    // The abstract drawing, when this map has one and it is wanted. It is
    // rendered for exactly the region on screen, so it is drawn as one image
    // rather than as tiles.
    bool drewAbstract = false;
    if (state.preferAbstract && abstractMap) {
        const auto wantMin = map::screenToBase(state.viewport, geo::Vec2{0.0, 0.0});
        const auto wantMax = map::screenToBase(
            state.viewport, geo::Vec2{static_cast<double>(width),
                                      static_cast<double>(height)});
        const AbstractMapImage image = abstractMap(wantMin, wantMax, width, height);

        if (image.available) {
            if (image.texture != 0) {
                const auto topLeft = map::baseToScreen(state.viewport, image.imageMin);
                const auto bottomRight =
                    map::baseToScreen(state.viewport, image.imageMax);
                draw->AddImage(image.texture,
                               ImVec2(origin.x + static_cast<float>(topLeft.x),
                                      origin.y + static_cast<float>(topLeft.y)),
                               ImVec2(origin.x + static_cast<float>(bottomRight.x),
                                      origin.y + static_cast<float>(bottomRight.y)),
                               ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), tileTint);
                ++result.tilesDrawn;
            }
            // Keep asking for frames while a render is in flight, or the new
            // image would not appear until something else moved.
            if (image.rendering || image.texture == 0) {
                ++result.tilesRequested;
            }
            drewAbstract = true;
        }
    }

    // Three maps publish no raster tiles at all -- Streets, Lighthouse and
    // Terminal are SVG-only upstream, confirmed by probing the CDN. Drawing a
    // grid of empty placeholders for them would look like a broken download
    // rather than missing data, so say what is actually wrong.
    if (!drewAbstract && map.tilePath.empty()) {
        const ImVec2 centre(origin.x + canvasSize.x / 2.0f,
                            origin.y + canvasSize.y / 2.0f);
        const char* headline = tr(Str::NoMapImageHeadline);
        const char* detail = tr(Str::NoMapImageDetail);
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
    const auto visible =
        drewAbstract ? std::vector<map::VisibleTile>{}
                     : map::visibleTiles(map, state.viewport, zoom);

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

    // Points of interest, drawn bottom to top by importance: spawns, extracts,
    // transits, quests, then the player, so the player is never hidden.
    {
        const double playerElevation = snapshot.fix.valid ? snapshot.fix.world.y : 0.0;
        const ImVec2 mouse = ImGui::GetIO().MousePos;

        struct Hover {
            std::string title;
            std::string detail;
            std::string note;
            ImVec2 at;
            float distanceSq = 1e30f;
        } hover;

        const auto toScreen = [&](const geo::Vec3& world, ImVec2& out) {
            const auto screen =
                map::baseToScreen(state.viewport, projection.toImage(world));
            if (screen.x < -32.0 || screen.y < -32.0 ||
                screen.x > state.viewport.screenWidth + 32.0 ||
                screen.y > state.viewport.screenHeight + 32.0) {
                return false;  // Off screen: nothing to draw or hit-test.
            }
            out = ImVec2(origin.x + static_cast<float>(screen.x),
                         origin.y + static_cast<float>(screen.y));
            return true;
        };
        const auto onLevel = [&](const std::optional<double>& top,
                                 const std::optional<double>& bottom) {
            if (!state.dimOtherFloors || !snapshot.fix.valid) {
                return true;
            }
            if (bottom && playerElevation < *bottom) return false;
            if (top && playerElevation > *top) return false;
            return true;
        };
        const auto consider = [&](const ImVec2& at, const std::string& title,
                                  const std::string& detail, const std::string& note) {
            // Any real pointer will do: the overlay's own while interactive,
            // or the game's, passed through, while it is free in a menu or
            // the inventory.
            if (!ImGui::IsMousePosValid()) {
                return;
            }
            const float dx = mouse.x - at.x;
            const float dy = mouse.y - at.y;
            const float d = dx * dx + dy * dy;
            if (d < kHoverRadius * kHoverRadius && d < hover.distanceSq) {
                hover = Hover{title, detail, note, at, d};
            }
        };

        struct Label {
            ImVec2 at;
            float iconHalf;
            const std::string* text;
            float alpha;
        };
        std::vector<Label> labels;

        // Exits and spawns are never dimmed by floor. Dimming makes sense for
        // a quest objective inside a building; an exit matters wherever you
        // stand, and whether its zone happens to carry a height band in the
        // data would otherwise decide which exits look greyed out.
        const auto drawPoints = [&](const std::vector<MapPoint>& points, float size,
                                    bool labelled) {
            for (const auto& point : points) {
                ImVec2 at;
                if (!toScreen(point.world, at)) {
                    continue;
                }
                const bool level = true;
                const float alpha = level ? 1.0f : 0.4f;
                drawIcon(draw, overlays.icons, point.icon, at, size, alpha);
                consider(at, point.label, {}, level ? std::string{} : std::string(tr(Str::OtherFloor)));
                if (labelled && !point.label.empty()) {
                    labels.push_back(Label{at, size / 2.0f, &point.label, alpha});
                }
            }
        };

        if (state.showSpawns) drawPoints(overlays.spawns, kSpawnSize, false);
        if (state.showExtracts) drawPoints(overlays.extracts, kExtractSize, true);
        if (state.showTransits) drawPoints(overlays.transits, kExtractSize, true);

        if (state.showQuests) {
            for (const auto& place : overlays.quests) {
                ImVec2 at;
                if (!toScreen(place.world, at)) {
                    continue;
                }
                const bool level = place.onSameLevelAs(playerElevation) ||
                                   !state.dimOtherFloors || !snapshot.fix.valid;
                // Optional objectives are drawn smaller rather than in a
                // different colour, so the icon keeps its meaning.
                const float size = place.optional ? kOptionalQuestSize : kQuestSize;
                drawQuestMarker(draw, overlays, place, at, size, level ? 1.0f : 0.4f);
                std::string note;
                if (place.optional) note = tr(Str::Optional);
                if (place.isQuestItem) {
                    note += note.empty() ? "" : ", ";
                    note += tr(Str::OneOfSeveralSpawns);
                }
                if (!level) {
                    note += note.empty() ? "" : ", ";
                    note += tr(Str::OtherFloor);
                }
                consider(at, place.taskName, place.detail, note);
            }
        }

        // Extract names, drawn after the quest markers so no icon covers them.
        // A label that would overlap one already placed is skipped: zoomed out,
        // 27 names on Customs would otherwise pile into an unreadable block,
        // and the hover tooltip still names every extract.
        if (!labels.empty()) {
            ImGui::PushFont(nullptr, kLabelFontSize);
            const ScreenRect screen{origin, ImVec2(origin.x + canvasSize.x,
                                                   origin.y + canvasSize.y)};
            std::vector<ScreenRect> placed;
            for (const auto& label : labels) {
                const ImVec2 size = ImGui::CalcTextSize(label.text->c_str());
                const float padX = 4.0f;
                const float padY = 1.0f;
                const auto box = placeMarkerLabel(
                    label.at, label.iconHalf,
                    ImVec2(size.x + padX * 2.0f, size.y + padY * 2.0f), screen, placed);
                if (!box) {
                    continue;
                }
                placed.push_back(*box);
                draw->AddRectFilled(box->min, box->max,
                                    rgba(0.04f, 0.05f, 0.05f, 0.78f * label.alpha), 3.0f);
                draw->AddText(ImVec2(box->min.x + padX, box->min.y + padY),
                              rgba(0.90f, 0.91f, 0.87f, label.alpha), label.text->c_str());
            }
            ImGui::PopFont();
        }

        if (snapshot.fix.valid) {
            ImVec2 at;
            if (toScreen(snapshot.fix.world, at)) {
                const double heading =
                    projection.toImageHeading(snapshot.fix.headingDegrees);
                const float pulse = pulsePhase(state.pulseFromMillis, nowMillis);
                drawPlayerMarker(draw, at, heading, snapshot.fix.ageSeconds(nowMillis),
                                 pulse);
                if (pulse >= 0.0f) {
                    result.wantsRedraw = true;  // until the pulse has run out
                }
            }
        }

        if (hover.distanceSq < 1e29f && !hover.title.empty()) {
            ImGui::SetNextWindowPos(ImVec2(hover.at.x + 16.0f, hover.at.y + 16.0f));
            ImGui::SetNextWindowBgAlpha(0.92f);
            if (ImGui::Begin("tarkov-help-tip", nullptr,
                             ImGuiWindowFlags_NoDecoration |
                                 ImGuiWindowFlags_AlwaysAutoResize |
                                 ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoFocusOnAppearing)) {
                ImGui::TextUnformatted(hover.title.c_str());
                if (!hover.detail.empty()) {
                    ImGui::PushTextWrapPos(420.0f);
                    ImGui::TextColored(ImVec4(0.72f, 0.74f, 0.68f, 1.0f), "%s",
                                       hover.detail.c_str());
                    ImGui::PopTextWrapPos();
                }
                if (!hover.note.empty()) {
                    ImGui::TextColored(ImVec4(0.55f, 0.57f, 0.52f, 1.0f), "%s",
                                       hover.note.c_str());
                }
            }
            ImGui::End();
        }
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
            state.viewport = map::clampToBounds(map, state.viewport);
            result.wantsRedraw = true;
        }
    }

    // Floor selector, only while the overlay takes input.
    if (interactive && !map.layers.empty()) {
        ImGui::SetNextWindowPos(ImVec2(origin.x + 16.0f, origin.y + 16.0f),
                                ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.85f);
        if (ImGui::Begin("tarkov-help-floors", nullptr,
                         ImGuiWindowFlags_NoDecoration |
                             ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove)) {
            // Opacity and layers live in the settings window; the in-game
            // panel keeps only the floor, a moment-to-moment choice. It is
            // only shown for maps that have floors to choose between.
            if (ImGui::Checkbox(tr(Str::FloorAuto), &state.layerAuto)) {
                result.wantsRedraw = true;
            }
            ImGui::Separator();
            // Listed top-down, the way a building reads.
            for (int index = static_cast<int>(map.layers.size()) - 1; index >= -1;
                 --index) {
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
        char text[512];
        const char* mode = state.followPlayer ? tr(Str::Following) : tr(Str::FreeView);
        // Interchange and Shoreline declare upper floors with no tiles. Saying
        // so beats silently showing the ground level and letting the player
        // believe they are looking at the floor they are standing on.
        const bool floorMissing = state.activeLayer >= 0 && !activeLayerDrawable;

        // The age of the fix is the one thing the map cannot show by itself and
        // the game does not show at all. It decides whether the marker means
        // anything, so it belongs where the marker is.
        std::string age = "   ";
        if (snapshot.fix.valid) {
            const double seconds = snapshot.fix.ageSeconds(nowMillis);
            age += trf(Str::FixAge, formatFixAge(seconds).c_str());
            if (seconds >= 60.0) {
                age += tr(Str::PressScreenshot);
            }
        } else {
            age += tr(Str::NoPositionPressScreenshot);
        }

        std::string questText;
        if (!overlays.quests.empty()) {
            questText = "   " + trf(Str::ObjectivesCount,
                                    static_cast<int>(overlays.quests.size()));
        }
        const char* renderer = drewAbstract ? tr(Str::RendererAbstract) : tr(Str::RendererSatellite);
        const std::string mapName = mapDisplayName(map.key);
        std::snprintf(text, sizeof(text), "%s (%s)   %s%s%s   %s%s%s",
                      mapName.c_str(), renderer,
                      layerName(map, state.activeLayer).c_str(),
                      state.layerAuto ? tr(Str::AutoSuffix) : "",
                      floorMissing ? tr(Str::NoImageForFloor) : "", mode, age.c_str(),
                      questText.c_str());

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
