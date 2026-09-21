#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace tarkov::map {

/// A rasterised image in RGBA8, ready to become a texture.
struct RenderedImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;  ///< RGBA8, row-major, no padding.

    bool valid() const {
        return width > 0 && height > 0 &&
               pixels.size() == static_cast<std::size_t>(width) * height * 4;
    }
};

/// The natural size of an SVG document, from its viewBox. Zero when the
/// document does not parse.
struct SvgSize {
    double width = 0.0;
    double height = 0.0;
};

SvgSize svgNaturalSize(std::string_view svg);

/// Rasterises a tarkov.dev abstract map, showing only the named layer groups.
///
/// The maps carry every floor in one document -- `Ground_Level`,
/// `Second_Floor`, `Underground_Level` and so on -- with all but the ground one
/// hidden. Visibility is set through an injected stylesheet rather than by
/// editing elements, which keeps the document untouched and the rule explicit.
///
/// A layer id that the document does not contain is ignored rather than
/// treated as an error: which floors exist varies by map, and maps.json and the
/// SVG do not always agree.
///
/// Returns an empty image when the document does not parse or the size is
/// unreasonable. Callers should degrade to no map rather than fail.
RenderedImage renderSvgLayers(std::string_view svg,
                              const std::vector<std::string>& visibleLayerIds,
                              int width, int height);

/// A rectangle in the SVG's own coordinate space.
struct SvgRect {
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
};

/// Rasterises one region of the map at exactly the requested pixel size.
///
/// This is what makes an abstract map usable at any zoom. Rendering the whole
/// document at a resolution high enough for a zoomed-in view would cost
/// hundreds of megabytes; rendering only what is on screen, at screen
/// resolution, stays sharp and costs one texture.
///
/// Measured on this machine at 2560px wide: Customs 32 ms, Streets 91 ms --
/// fast enough to redo on a worker thread when the viewport moves.
RenderedImage renderSvgRegion(std::string_view svg,
                              const std::vector<std::string>& visibleLayerIds,
                              const SvgRect& region, int width, int height);

/// A parsed document, so a map can be rasterised repeatedly without re-parsing
/// it. Parsing Streets takes longer than drawing it.
class SvgDocument {
public:
    SvgDocument();
    ~SvgDocument();
    SvgDocument(SvgDocument&&) noexcept;
    SvgDocument& operator=(SvgDocument&&) noexcept;
    SvgDocument(const SvgDocument&) = delete;
    SvgDocument& operator=(const SvgDocument&) = delete;

    /// Parses and applies the layer visibility. Returns false if it will not parse.
    bool load(std::string_view svg, const std::vector<std::string>& visibleLayerIds);

    bool valid() const;
    SvgSize size() const;

    RenderedImage renderRegion(const SvgRect& region, int width, int height) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace tarkov::map
