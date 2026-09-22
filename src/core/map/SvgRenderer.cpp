#include "core/map/SvgRenderer.h"

#include <algorithm>
#include <memory>

#include <lunasvg.h>

namespace tarkov::map {
namespace {

/// A ceiling on what we will allocate for one rasterisation. 8192x8192 RGBA is
/// 256 MB, which is already far past anything useful for an overlay.
constexpr int kMaxDimension = 8192;

std::unique_ptr<lunasvg::Document> load_(std::string_view svg) {
    if (svg.empty()) {
        return nullptr;
    }
    return lunasvg::Document::loadFromData(svg.data(), svg.size());
}

}  // namespace

SvgSize svgNaturalSize(std::string_view svg) {
    const auto document = load_(svg);
    if (!document) {
        return {};
    }
    return SvgSize{document->width(), document->height()};
}

namespace {

/// The value of `name="..."` inside one tag's text, or empty.
std::string_view attributeIn(std::string_view tag, std::string_view name) {
    std::size_t at = 0;
    while ((at = tag.find(name, at)) != std::string_view::npos) {
        // A whole attribute name: preceded by space, followed by ="
        const bool starts = at > 0 && (tag[at - 1] == ' ' || tag[at - 1] == '\t' ||
                                       tag[at - 1] == '\n' || tag[at - 1] == '\r');
        const std::size_t valueAt = at + name.size() + 2;
        if (starts && tag.substr(at + name.size(), 2) == "=\"") {
            const std::size_t end = tag.find('"', valueAt);
            if (end != std::string_view::npos) {
                return tag.substr(valueAt, end - valueAt);
            }
        }
        at += name.size();
    }
    return {};
}

struct DeclaredLayer {
    std::string_view id;
    std::string_view keptWith;  ///< data-keep-with-group, or empty
};

/// The floor groups a document declares: every <g> with a data-layer.
std::vector<DeclaredLayer> declaredLayers(std::string_view svg) {
    std::vector<DeclaredLayer> layers;
    std::size_t at = 0;
    while ((at = svg.find("<g", at)) != std::string_view::npos) {
        const std::size_t end = svg.find('>', at);
        if (end == std::string_view::npos) {
            break;
        }
        const std::string_view tag = svg.substr(at, end - at);
        at = end;
        if (tag.size() > 2 && tag[2] != ' ' && tag[2] != '\t' && tag[2] != '\n' &&
            tag[2] != '\r') {
            continue;  // <glyph>, <gradient>: not a group
        }
        if (attributeIn(tag, "data-layer").empty()) {
            continue;
        }
        const std::string_view id = attributeIn(tag, "id");
        if (!id.empty()) {
            layers.push_back({id, attributeIn(tag, "data-keep-with-group")});
        }
    }
    return layers;
}

/// Shows exactly the floors asked for.
///
/// tarkov.dev's documents draw every floor as authored and leave hiding them
/// to the page, so each floor group the document declares with data-layer is
/// hidden unless asked for, or kept with a group that is (First_Floor goes
/// with Ground_Level). Once, only the asked-for groups were forced visible and
/// nothing was hidden, so every floor of every map drew stacked on every
/// floor -- found 2026-09-22 when three floors of Dorms rendered to the same
/// pixels. With no floors named, the document draws as authored.
void applyLayerVisibility(lunasvg::Document& document, std::string_view svg,
                          const std::vector<std::string>& visibleLayerIds) {
    if (visibleLayerIds.empty()) {
        return;
    }
    const auto visible = [&](std::string_view id) {
        return !id.empty() && std::find(visibleLayerIds.begin(), visibleLayerIds.end(),
                                        id) != visibleLayerIds.end();
    };
    std::string sheet;
    for (const auto& layer : declaredLayers(svg)) {
        if (!visible(layer.id) && !visible(layer.keptWith)) {
            sheet += "#";
            sheet += layer.id;
            sheet += "{display:none!important}";
        }
    }
    for (const auto& id : visibleLayerIds) {
        if (id.empty()) {
            continue;
        }
        sheet += "#";
        sheet += id;
        sheet += "{display:inline!important;visibility:visible!important}";
    }
    if (!sheet.empty()) {
        document.applyStyleSheet(sheet);
        document.updateLayout();
    }
}

RenderedImage bitmapToImage(lunasvg::Bitmap& bitmap) {
    RenderedImage image;
    if (bitmap.isNull() || bitmap.data() == nullptr) {
        return image;
    }
    // lunasvg renders premultiplied ARGB32; the GPU wants plain RGBA.
    bitmap.convertToRGBA();

    image.width = bitmap.width();
    image.height = bitmap.height();
    image.pixels.resize(static_cast<std::size_t>(image.width) * image.height * 4);

    const int stride = bitmap.stride();
    const std::uint8_t* source = bitmap.data();
    for (int y = 0; y < image.height; ++y) {
        std::copy(source + static_cast<std::size_t>(y) * stride,
                  source + static_cast<std::size_t>(y) * stride + image.width * 4,
                  image.pixels.begin() +
                      static_cast<std::size_t>(y) * image.width * 4);
    }
    return image;
}

}  // namespace

RenderedImage renderSvgLayers(std::string_view svg,
                              const std::vector<std::string>& visibleLayerIds,
                              int width, int height) {
    if (width <= 0 || height <= 0 || width > kMaxDimension ||
        height > kMaxDimension) {
        return {};
    }
    auto document = load_(svg);
    if (!document) {
        return {};
    }
    applyLayerVisibility(*document, svg, visibleLayerIds);
    lunasvg::Bitmap bitmap = document->renderToBitmap(width, height);
    return bitmapToImage(bitmap);
}

struct SvgDocument::Impl {
    std::unique_ptr<lunasvg::Document> document;
};

SvgDocument::SvgDocument() : impl_(std::make_unique<Impl>()) {}
SvgDocument::~SvgDocument() = default;
SvgDocument::SvgDocument(SvgDocument&&) noexcept = default;
SvgDocument& SvgDocument::operator=(SvgDocument&&) noexcept = default;

bool SvgDocument::load(std::string_view svg,
                       const std::vector<std::string>& visibleLayerIds) {
    impl_->document = load_(svg);
    if (!impl_->document) {
        return false;
    }
    applyLayerVisibility(*impl_->document, svg, visibleLayerIds);
    return true;
}

bool SvgDocument::valid() const { return impl_ && impl_->document != nullptr; }

SvgSize SvgDocument::size() const {
    if (!valid()) {
        return {};
    }
    return SvgSize{impl_->document->width(), impl_->document->height()};
}

RenderedImage SvgDocument::renderRegion(const SvgRect& region, int width,
                                        int height) const {
    RenderedImage image;
    if (!valid() || width <= 0 || height <= 0 || width > kMaxDimension ||
        height > kMaxDimension || region.width <= 0.0 || region.height <= 0.0) {
        return image;
    }

    lunasvg::Bitmap bitmap(width, height);
    if (bitmap.isNull()) {
        return image;
    }
    bitmap.clear(0x00000000);

    // Map the requested region onto the whole bitmap: scale first, then shift
    // the region's origin to the top-left corner.
    const double scaleX = width / region.width;
    const double scaleY = height / region.height;
    const lunasvg::Matrix matrix(static_cast<float>(scaleX), 0.0f, 0.0f,
                                 static_cast<float>(scaleY),
                                 static_cast<float>(-region.x * scaleX),
                                 static_cast<float>(-region.y * scaleY));
    impl_->document->render(bitmap, matrix);
    return bitmapToImage(bitmap);
}

RenderedImage renderSvgRegion(std::string_view svg,
                              const std::vector<std::string>& visibleLayerIds,
                              const SvgRect& region, int width, int height) {
    SvgDocument document;
    if (!document.load(svg, visibleLayerIds)) {
        return {};
    }
    return document.renderRegion(region, width, height);
}

}  // namespace tarkov::map
