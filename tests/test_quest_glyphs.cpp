#include <doctest/doctest.h>

#include <set>
#include <string>

#include "ui/QuestGlyphs.h"

using namespace tarkov;
using data::ObjectiveKind;

TEST_CASE("every objective kind with a glyph renders one, in white") {
    std::set<std::string> names;
    for (const auto kind : {ObjectiveKind::Visit, ObjectiveKind::Mark, ObjectiveKind::Plant,
                            ObjectiveKind::Find, ObjectiveKind::Shoot, ObjectiveKind::Use}) {
        const auto image = ui::renderQuestGlyph(kind, 32);
        REQUIRE(image.valid());
        CHECK(image.width == 32);

        // Something is drawn, and every drawn pixel is white: the colour comes
        // from the tint at draw time, so it can fade for another floor.
        int opaque = 0;
        bool allWhite = true;
        for (std::size_t i = 0; i < image.pixels.size(); i += 4) {
            if (image.pixels[i + 3] > 200) {
                ++opaque;
                allWhite = allWhite && image.pixels[i] > 240 && image.pixels[i + 1] > 240 &&
                           image.pixels[i + 2] > 240;
            }
        }
        CHECK(opaque > 20);
        CHECK(allWhite);
        names.insert(ui::questGlyphName(kind));
    }
    CHECK(names.size() == 6);  // No two kinds share a texture.
}

TEST_CASE("an objective with no glyph of its own renders nothing") {
    CHECK(ui::questGlyphSvg(ObjectiveKind::Other).empty());
    CHECK_FALSE(ui::renderQuestGlyph(ObjectiveKind::Other, 32).valid());
}
