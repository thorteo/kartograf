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

RenderedImage renderSvgLayers(std::string_view svg,
                              const std::vector<std::string>& visibleLayerIds,
                              int width, int height) {
    RenderedImage image;
    if (width <= 0 || height <= 0 || width > kMaxDimension ||
        height > kMaxDimension) {
        return image;
    }

    auto document = load_(svg);
    if (!document) {
        return image;
    }

    if (!visibleLayerIds.empty()) {
        // Show exactly the layers asked for. Ids that are not in the document
        // simply match nothing, which is what we want -- maps.json names floors
        // that some SVGs do not carry.
        std::string sheet;
        for (const auto& id : visibleLayerIds) {
            if (id.empty()) {
                continue;
            }
            sheet += "#";
            sheet += id;
            sheet += "{display:inline!important;visibility:visible!important}";
        }
        if (!sheet.empty()) {
            document->applyStyleSheet(sheet);
            document->updateLayout();
        }
    }

    lunasvg::Bitmap bitmap = document->renderToBitmap(width, height);
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

namespace {

void applyLayerVisibility(lunasvg::Document& document,
                          const std::vector<std::string>& visibleLayerIds) {
    if (visibleLayerIds.empty()) {
        return;
    }
    std::string sheet;
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
    applyLayerVisibility(*impl_->document, visibleLayerIds);
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
