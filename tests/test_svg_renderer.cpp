#include <doctest/doctest.h>

#include "core/map/SvgRenderer.h"
#include "fixtures.h"

using namespace tarkov::map;

namespace {

struct Rgba {
    int r = 0, g = 0, b = 0, a = 0;
    bool operator==(const Rgba& other) const {
        return r == other.r && g == other.g && b == other.b && a == other.a;
    }
    bool operator!=(const Rgba& other) const { return !(*this == other); }
};

Rgba pixelAt(const RenderedImage& image, int x, int y) {
    const std::size_t at = (static_cast<std::size_t>(y) * image.width + x) * 4;
    return Rgba{image.pixels[at], image.pixels[at + 1], image.pixels[at + 2],
                image.pixels[at + 3]};
}

std::string layersSvg() {
    return tarkov::test::readFile(tarkov::test::fixturePath("svg/layers.svg"));
}

}  // namespace

TEST_CASE("an SVG reports its natural size") {
    const SvgSize size = svgNaturalSize(layersSvg());
    CHECK(size.width == doctest::Approx(100.0));
    CHECK(size.height == doctest::Approx(100.0));

    CHECK(svgNaturalSize("").width == doctest::Approx(0.0));
    CHECK(svgNaturalSize("not svg at all").width == doctest::Approx(0.0));
}

TEST_CASE("only the ground level draws when nothing else is asked for") {
    const RenderedImage image = renderSvgLayers(layersSvg(), {"Ground_Level"}, 100, 100);
    REQUIRE(image.valid());
    CHECK(image.width == 100);
    CHECK(image.height == 100);

    // Left half is the ground rectangle.
    CHECK(pixelAt(image, 25, 50) == Rgba{0, 0, 255, 255});
    // The document draws every floor; the ones not asked for are hidden, so
    // the right half is transparent. Before this, all floors drew stacked on
    // every floor of every real map.
    CHECK(pixelAt(image, 75, 25).a == 0);
    CHECK(pixelAt(image, 75, 75).a == 0);
    // The basement is hidden too, leaving the ground beneath it.
    CHECK(pixelAt(image, 25, 90) == Rgba{0, 0, 255, 255});
    // A group kept with the ground shows with it.
    CHECK(pixelAt(image, 25, 10) == Rgba{0, 255, 255, 255});
}

TEST_CASE("the basement shows when asked for") {
    const RenderedImage image =
        renderSvgLayers(layersSvg(), {"Ground_Level", "Underground_Level"}, 100, 100);
    REQUIRE(image.valid());
    CHECK(pixelAt(image, 25, 90) == Rgba{255, 255, 0, 255});
    CHECK(pixelAt(image, 75, 25).a == 0);
}

TEST_CASE("with no floors named, the document draws as authored") {
    // The quest glyphs are rendered this way: they have no floors at all.
    const RenderedImage image = renderSvgLayers(layersSvg(), {}, 100, 100);
    REQUIRE(image.valid());
    CHECK(pixelAt(image, 75, 25) == Rgba{255, 0, 0, 255});
    CHECK(pixelAt(image, 75, 75) == Rgba{0, 255, 0, 255});
}

TEST_CASE("asking for a floor reveals exactly that floor") {
    const RenderedImage image =
        renderSvgLayers(layersSvg(), {"Ground_Level", "Second_Floor"}, 100, 100);
    REQUIRE(image.valid());

    CHECK(pixelAt(image, 25, 50) == Rgba{0, 0, 255, 255});   // ground, still there
    CHECK(pixelAt(image, 75, 25) == Rgba{255, 0, 0, 255});   // second floor, now shown
    CHECK(pixelAt(image, 75, 75).a == 0);                    // third floor, still hidden
}

TEST_CASE("a floor the document does not have is ignored, not fatal") {
    // maps.json names floors that some SVGs do not carry -- Customs declares a
    // 4th floor with no SVG layer at all. That must render the rest, not fail.
    const RenderedImage image = renderSvgLayers(
        layersSvg(), {"Ground_Level", "Fourth_Floor", "Bunkers"}, 100, 100);
    REQUIRE(image.valid());
    CHECK(pixelAt(image, 25, 50) == Rgba{0, 0, 255, 255});
    CHECK(pixelAt(image, 75, 25).a == 0);
}

TEST_CASE("rendering scales to the requested size") {
    const RenderedImage small = renderSvgLayers(layersSvg(), {"Ground_Level"}, 40, 40);
    REQUIRE(small.valid());
    CHECK(small.width == 40);
    CHECK(small.height == 40);
    CHECK(pixelAt(small, 10, 20) == Rgba{0, 0, 255, 255});

    const RenderedImage large =
        renderSvgLayers(layersSvg(), {"Ground_Level"}, 512, 512);
    REQUIRE(large.valid());
    CHECK(large.width == 512);
    CHECK(pixelAt(large, 128, 256) == Rgba{0, 0, 255, 255});
}

TEST_CASE("bad input degrades to no map rather than a crash") {
    for (const char* bad : {"", "<svg", "{}", "<html><body/></html>"}) {
        CAPTURE(bad);
        CHECK_FALSE(renderSvgLayers(bad, {"Ground_Level"}, 64, 64).valid());
    }

    // Sizes that would allocate absurd amounts are refused outright.
    CHECK_FALSE(renderSvgLayers(layersSvg(), {}, 0, 100).valid());
    CHECK_FALSE(renderSvgLayers(layersSvg(), {}, 100, -5).valid());
    CHECK_FALSE(renderSvgLayers(layersSvg(), {}, 100000, 100000).valid());
}

TEST_CASE("pixels come back as plain RGBA, not premultiplied ARGB") {
    // lunasvg renders premultiplied ARGB32 internally. Skipping the conversion
    // would tint every map and swap its red and blue channels -- subtle enough
    // to look like a bad colour choice rather than a bug.
    const RenderedImage image = renderSvgLayers(layersSvg(), {"Ground_Level"}, 64, 64);
    REQUIRE(image.valid());

    const Rgba blue = pixelAt(image, 16, 32);
    CHECK(blue.r == 0);
    CHECK(blue.g == 0);
    CHECK(blue.b == 255);
    CHECK(blue.a == 255);
}

TEST_CASE("a region renders at the requested pixel size") {
    // The right half of the fixture holds the two upper-floor rectangles.
    const SvgRect rightHalf{50.0, 0.0, 50.0, 100.0};
    const RenderedImage image = renderSvgRegion(
        layersSvg(), {"Ground_Level", "Second_Floor"}, rightHalf, 100, 200);

    REQUIRE(image.valid());
    CHECK(image.width == 100);
    CHECK(image.height == 200);

    // The second floor fills the top half of this region...
    CHECK(pixelAt(image, 50, 50) == Rgba{255, 0, 0, 255});
    // ...and the hidden third floor leaves the bottom transparent.
    CHECK(pixelAt(image, 50, 150).a == 0);
    // The ground rectangle is entirely outside this region.
    CHECK(pixelAt(image, 2, 100) != Rgba{0, 0, 255, 255});
}

TEST_CASE("zooming into a region keeps it sharp rather than scaling it up") {
    // A tiny region blown up to a large bitmap is the case that matters: it is
    // what happens when the player zooms in, and it is why regions are
    // rendered at all instead of scaling one full-map texture.
    const SvgRect corner{0.0, 40.0, 10.0, 10.0};  // bare ground, clear of First_Floor
    const RenderedImage image =
        renderSvgRegion(layersSvg(), {"Ground_Level"}, corner, 400, 400);

    REQUIRE(image.valid());
    CHECK(image.width == 400);
    // The whole region is inside the ground rectangle, so every pixel is it.
    CHECK(pixelAt(image, 10, 10) == Rgba{0, 0, 255, 255});
    CHECK(pixelAt(image, 200, 200) == Rgba{0, 0, 255, 255});
    CHECK(pixelAt(image, 390, 390) == Rgba{0, 0, 255, 255});
}

TEST_CASE("a parsed document renders repeatedly without re-parsing") {
    // Parsing Streets costs more than drawing it, so panning must not re-parse.
    SvgDocument document;
    REQUIRE(document.load(layersSvg(), {"Ground_Level", "Second_Floor"}));
    CHECK(document.valid());
    CHECK(document.size().width == doctest::Approx(100.0));

    const RenderedImage left =
        document.renderRegion(SvgRect{0.0, 0.0, 50.0, 100.0}, 100, 200);
    const RenderedImage right =
        document.renderRegion(SvgRect{50.0, 0.0, 50.0, 100.0}, 100, 200);

    REQUIRE(left.valid());
    REQUIRE(right.valid());
    CHECK(pixelAt(left, 50, 100) == Rgba{0, 0, 255, 255});
    CHECK(pixelAt(right, 50, 50) == Rgba{255, 0, 0, 255});
}

TEST_CASE("an unparseable document reports itself rather than rendering junk") {
    SvgDocument document;
    CHECK_FALSE(document.load("<not-svg/>garbage", {"Ground_Level"}));
    CHECK_FALSE(document.valid());
    CHECK_FALSE(document.renderRegion(SvgRect{0, 0, 10, 10}, 64, 64).valid());
}

TEST_CASE("a degenerate region is refused") {
    SvgDocument document;
    REQUIRE(document.load(layersSvg(), {"Ground_Level"}));
    CHECK_FALSE(document.renderRegion(SvgRect{0, 0, 0, 10}, 64, 64).valid());
    CHECK_FALSE(document.renderRegion(SvgRect{0, 0, 10, -1}, 64, 64).valid());
    CHECK_FALSE(document.renderRegion(SvgRect{0, 0, 10, 10}, 0, 64).valid());
    CHECK_FALSE(document.renderRegion(SvgRect{0, 0, 10, 10}, 99999, 99999).valid());
}
