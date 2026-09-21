#include "ui/QuestGlyphs.h"

#include <string>

namespace tarkov::ui {

// Generated from third_party/game-icons/icons; see CMakeLists.txt.
extern const std::string_view kGlyph_radio_tower;
extern const std::string_view kGlyph_cardboard_box;
extern const std::string_view kGlyph_magnifying_glass;
extern const std::string_view kGlyph_light_backpack;
extern const std::string_view kGlyph_crosshair;
extern const std::string_view kGlyph_hand;

std::string_view questGlyphSvg(data::ObjectiveKind kind) {
    switch (kind) {
        case data::ObjectiveKind::Visit: return kGlyph_magnifying_glass;
        case data::ObjectiveKind::Mark: return kGlyph_radio_tower;
        case data::ObjectiveKind::Plant: return kGlyph_cardboard_box;
        case data::ObjectiveKind::Find: return kGlyph_light_backpack;
        case data::ObjectiveKind::Shoot: return kGlyph_crosshair;
        case data::ObjectiveKind::Use: return kGlyph_hand;
        case data::ObjectiveKind::Other: return {};
    }
    return {};
}

std::string questGlyphName(data::ObjectiveKind kind) {
    switch (kind) {
        case data::ObjectiveKind::Visit: return "glyph-visit";
        case data::ObjectiveKind::Mark: return "glyph-mark";
        case data::ObjectiveKind::Plant: return "glyph-plant";
        case data::ObjectiveKind::Find: return "glyph-find";
        case data::ObjectiveKind::Shoot: return "glyph-shoot";
        case data::ObjectiveKind::Use: return "glyph-use";
        case data::ObjectiveKind::Other: return {};
    }
    return {};
}

map::RenderedImage renderQuestGlyph(data::ObjectiveKind kind, int size) {
    std::string svg(questGlyphSvg(kind));
    if (svg.empty()) {
        return {};
    }
    // game-icons.net files are a white glyph on a black square. Drop the
    // square: the glyph alone, in white, so the draw call's tint decides the
    // colour and can fade it for another floor.
    constexpr std::string_view kBackground = "<path d=\"M0 0h512v512H0z\"/>";
    if (const auto at = svg.find(kBackground); at != std::string::npos) {
        svg.erase(at, kBackground.size());
    }
    return map::renderSvgLayers(svg, {}, size, size);
}

}  // namespace tarkov::ui
