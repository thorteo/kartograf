#include "ui/Strings.h"
#include "platform/win32/SvgMapStore.h"

#include <cmath>

#include "platform/win32/Files.h"
#include "platform/win32/HttpClient.h"
#include "platform/win32/Textures.h"

namespace tarkov::win32 {
namespace {

namespace fs = std::filesystem;

/// How long a map whose drawing could not be had is left alone before trying
/// again. Long enough not to hammer the CDN from an offline machine, short
/// enough that a network that comes back is noticed within the raid.
constexpr auto kRetryAfter = std::chrono::seconds(60);

bool sameLayers(const std::vector<std::string>& a,
                const std::vector<std::string>& b) {
    return a == b;
}

}  // namespace

SvgMapStore::~SvgMapStore() { stop(); }

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
    cacheRoot_ = appDataPath(L"svg");

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
    {
        // Under the lock, or a worker between its check and its wait misses
        // both the flag and the notification, and join() waits forever.
        const std::lock_guard<std::mutex> lock(mutex_);
        stopping_.store(true, std::memory_order_relaxed);
    }
    wake_.notify_all();
    cancel_.cancel();
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
        view.failed = failedGeneration_ == generation_;
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
        // Not while the drawing is known to be unobtainable: the request just
        // queued is only a retry probe, and nothing should wait on it.
        view.rendering = !view.failed && (rendering_ || havePending_);
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
    std::uint64_t generation = 0;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        key = mapKey_;
        url = svgUrl_;
        layers = layerIds_;
        generation = generation_;
        if (key.empty() || url.empty()) {
            return false;
        }
        if (key == loadedKey_ && sameLayers(layers, loadedLayers_) &&
            document_.valid()) {
            return true;
        }
        if (failedGeneration_ == generation &&
            std::chrono::steady_clock::now() - failedAt_ < kRetryAfter) {
            return false;
        }
    }
    const auto giveUp = [&] {
        const std::lock_guard<std::mutex> lock(mutex_);
        failedGeneration_ = generation;
        failedAt_ = std::chrono::steady_clock::now();
        return false;
    };

    const fs::path cached = cachePathFor(key);
    std::string svg = readTextFile(cached);
    if (svg.empty()) {
        HttpClient client;
        if (client.open(&cancel_)) {
            const auto response = client.get(url, 8u * 1024 * 1024);
            if (response.ok) {
                svg.assign(response.body.begin(), response.body.end());
                writeFileAtomic(cached, svg);
            } else if (!cancel_.cancelled()) {
                say(ui::trf(ui::Str::StatusDrawingDownloadFailed, ui::mapDisplayName(key).c_str()));
            }
        }
    }
    if (svg.empty()) {
        return giveUp();
    }

    map::SvgDocument document;
    if (!document.load(svg, layers)) {
        // A cached file that will not parse would fail forever; drop it.
        std::error_code ec;
        fs::remove(cached, ec);
        say(ui::trf(ui::Str::StatusDrawingDidNotParse, ui::mapDisplayName(key).c_str()));
        return giveUp();
    }
    const map::SvgSize size = document.size();

    const std::lock_guard<std::mutex> lock(mutex_);
    document_ = std::move(document);
    documentSize_ = size;
    loadedKey_ = key;
    loadedLayers_ = layers;
    failedGeneration_ = ~std::uint64_t{0};
    return true;
}

void SvgMapStore::run() {
    while (!stopping_.load(std::memory_order_relaxed)) {
        Request request;
        bool haveRequest = false;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this] {
                // A map with no drawing, or one that just failed, is not work:
                // waking for it would spin. A request from the render thread
                // still wakes the worker, which retries once kRetryAfter is up.
                return stopping_.load(std::memory_order_relaxed) || havePending_ ||
                       (!mapKey_.empty() && !svgUrl_.empty() &&
                        mapKey_ != loadedKey_ && failedGeneration_ != generation_);
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

    const auto view =
        createRgbaTexture(device_, image.width, image.height, image.pixels.data());
    if (!view) {
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
