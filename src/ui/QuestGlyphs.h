#pragma once

#include <string>
#include <string_view>

#include "core/data/TaskModel.h"
#include "core/map/SvgRenderer.h"

namespace tarkov::ui {

/// The glyph drawn inside a quest marker, saying what to do there: a magnifier
/// to locate a place, a radio tower to mark, a backpack to pick an item up, and
/// so on.
///
/// The glyphs are game-icons.net's (CC BY 3.0, third_party/game-icons),
/// compiled in.
/// Returns an empty view for ObjectiveKind::Other, which keeps tarkov.dev's
/// generic quest icon.
std::string_view questGlyphSvg(data::ObjectiveKind kind);

/// The texture name a glyph is stored under, e.g. "glyph-search".
std::string questGlyphName(data::ObjectiveKind kind);

/// Rasterises a glyph in white at `size` pixels, to be tinted when drawn.
/// Empty for a kind with no glyph.
map::RenderedImage renderQuestGlyph(data::ObjectiveKind kind, int size);

}  // namespace tarkov::ui
