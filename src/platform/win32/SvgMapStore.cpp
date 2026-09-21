#include "ui/Strings.h"
#include "platform/win32/SvgMapStore.h"

#include <shlobj.h>

#include <cmath>
#include <fstream>
#include <sstream>

#include "platform/win32/HttpClient.h"

namespace tarkov::win32 {
namespace {

namespace fs = std::filesystem;

std::string readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

void writeFile(const fs::path& path, const std::string& text) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    const fs::path temporary = path.string() + ".part";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) {
            return;
        }
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!out) {
            return;
        }
    }
    fs::rename(temporary, path, ec);
}

bool sameLayers(const std::vector<std::string>& a,
                const std::vector<std::string>& b) {
    return a == b;
}

}  // namespace

SvgMapStore::~SvgMapStore() { stop(); }

fs::path SvgMapStore::defaultCacheRoot() {
    PWSTR raw = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &raw))) {
        fs::path path(raw);
        CoTaskMemFree(raw);
        return path / "tarkov-help" / "svg";
    }
    return fs::temp_directory_path() / "tarkov-help" / "svg";
}

void SvgMapStore::say(const std::string& message) const {
    if (logger_) {
        logger_(message);
    }
}

fs::path SvgMapStore::cachePathFor(const std::string& mapKey) const {
    return cacheRoot_ / (mapKey + ".svg");
}

bool SvgMapStore::start(ID3D11Device* device, Logger logger) {
    if (device == nullptr) {
        return false;
    }
    device_ = device;
    logger_ = std::move(logger);
    cacheRoot_ = defaultCacheRoot();

    std::error_code ec;
    fs::create_directories(cacheRoot_, ec);

    stopping_.store(false, std::memory_order_relaxed);
    worker_ = std::thread(&SvgMapStore::run, this);
    return true;
}

void SvgMapStore::stop() {
    if (!worker_.joinable()) {
        return;
    }
    stopping_.store(true, std::memory_order_relaxed);
    wake_.notify_all();
    worker_.join();
}

void SvgMapStore::setMap(const std::string& mapKey, const std::string& svgUrl,
                         const std::vector<std::string>& layerIds) {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (mapKey_ == mapKey && svgUrl_ == svgUrl && sameLayers(layerIds_, layerIds)) {
            return;
        }
        mapKey_ = mapKey;
        svgUrl_ = svgUrl;
        layerIds_ = layerIds;
        // A new generation invalidates anything in flight: a render of the
        // previous floor must not be uploaded over the new one.
        ++generation_;
        havePending_ = false;
        haveFinished_ = false;
        texture_.reset();
    }
    wake_.notify_one();
}

map::SvgSize SvgMapStore::documentSize() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return documentSize_;
}

SvgMapStore::View SvgMapStore::acquire(const map::SvgRect& wanted, int width,
                                       int height) {
    View view;
    bool needWake = false;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        view.haveDocument = documentSize_.width > 0.0;
        view.rendering = rendering_;
        view.texture = texture_.get();
        view.region = textureRegion_;

        if (width > 0 && height > 0 && wanted.width > 0.0 && wanted.height > 0.0) {
            const bool differs =
                textureGeneration_ != generation_ ||
                std::abs(textureRegion_.x - wanted.x) > wanted.width * 0.002 ||
                std::abs(textureRegion_.y - wanted.y) > wanted.height * 0.002 ||
                std::abs(textureRegion_.width - wanted.width) >
                    wanted.width * 0.002 ||
                std::abs(textureRegion_.height - wanted.height) >
                    wanted.height * 0.002;

            if (differs) {
                // Only the latest request matters; panning replaces it rather
                // than queueing every intermediate position.
                pending_ = Request{wanted, width, height, generation_};
                havePending_ = true;
                needWake = true;
            }
        }
    }
    if (needWake) {
        wake_.notify_one();
    }
    return view;
}

bool SvgMapStore::ensureDocument() {
    std::string key;
    std::string url;
    std::vector<std::string> layers;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        key = mapKey_;
        url = svgUrl_;
        layers = layerIds_;
        if (key.empty() || url.empty()) {
            return false;
        }
        if (key == loadedKey_ && sameLayers(layers, loadedLayers_) &&
            document_.valid()) {
            return true;
        }
    }

    const fs::path cached = cachePathFor(key);
    std::string svg = readFile(cached);
    if (svg.empty()) {
        HttpClient client;
        if (client.open(L"tarkov-help/0.1")) {
            const auto response = client.get(url, 8u * 1024 * 1024);
            if (response.ok) {
                svg.assign(response.body.begin(), response.body.end());
                writeFile(cached, svg);
            } else {
                say(ui::trf(ui::Str::StatusDrawingDownloadFailed, ui::mapDisplayName(key).c_str()));
            }
        }
    }
    if (svg.empty()) {
        return false;
    }

    map::SvgDocument document;
    if (!document.load(svg, layers)) {
        // A cached file that will not parse would fail forever; drop it.
        std::error_code ec;
        fs::remove(cached, ec);
        say(ui::trf(ui::Str::StatusDrawingDidNotParse, ui::mapDisplayName(key).c_str()));
        return false;
    }
    const map::SvgSize size = document.size();

    const std::lock_guard<std::mutex> lock(mutex_);
    document_ = std::move(document);
    documentSize_ = size;
    loadedKey_ = key;
    loadedLayers_ = layers;
    return true;
}

void SvgMapStore::run() {
    while (!stopping_.load(std::memory_order_relaxed)) {
        Request request;
        bool haveRequest = false;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this] {
                return stopping_.load(std::memory_order_relaxed) || havePending_ ||
                       (!mapKey_.empty() && mapKey_ != loadedKey_);
            });
            if (stopping_.load(std::memory_order_relaxed)) {
                return;
            }
            if (havePending_) {
                request = pending_;
                havePending_ = false;
                haveRequest = true;
                rendering_ = true;
            }
        }

        if (!ensureDocument()) {
            const std::lock_guard<std::mutex> lock(mutex_);
            rendering_ = false;
            continue;
        }
        if (!haveRequest) {
            continue;
        }

        map::RenderedImage image;
        {
            // The document is only mutated by this thread, and the render
            // thread never touches it, so reading it unlocked is safe. The
            // lock is held only to copy what is needed.
            const std::lock_guard<std::mutex> lock(mutex_);
            if (request.generation != generation_ || !document_.valid()) {
                rendering_ = false;
                continue;
            }
        }
        image = document_.renderRegion(request.region, request.width,
                                       request.height);

        const std::lock_guard<std::mutex> lock(mutex_);
        rendering_ = false;
        if (image.valid() && request.generation == generation_) {
            finished_ = std::move(image);
            finishedRegion_ = request.region;
            finishedGeneration_ = request.generation;
            haveFinished_ = true;
        }
    }
}

bool SvgMapStore::pump() {
    map::RenderedImage image;
    map::SvgRect region;
    std::uint64_t generation = 0;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (!haveFinished_) {
            return false;
        }
        image = std::move(finished_);
        region = finishedRegion_;
        generation = finishedGeneration_;
        haveFinished_ = false;
        finished_ = {};
    }
    if (!image.valid()) {
        return false;
    }

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = static_cast<UINT>(image.width);
    desc.Height = static_cast<UINT>(image.height);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initial{};
    initial.pSysMem = image.pixels.data();
    initial.SysMemPitch = static_cast<UINT>(image.width * 4);

    ComPtr<ID3D11Texture2D> texture;
    if (FAILED(device_->CreateTexture2D(&desc, &initial, texture.put()))) {
        return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc{};
    viewDesc.Format = desc.Format;
    viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    viewDesc.Texture2D.MipLevels = 1;

    ComPtr<ID3D11ShaderResourceView> view;
    if (FAILED(device_->CreateShaderResourceView(texture.get(), &viewDesc,
                                                 view.put()))) {
        return false;
    }

    const std::lock_guard<std::mutex> lock(mutex_);
    if (generation != generation_) {
        return false;  // The map changed while this was uploading.
    }
    texture_ = view;
    textureRegion_ = region;
    textureGeneration_ = generation;
    return true;
}

}  // namespace tarkov::win32
