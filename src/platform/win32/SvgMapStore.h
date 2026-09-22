#pragma once

#include <windows.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <d3d11.h>

#include "core/map/SvgRenderer.h"
#include "platform/win32/ComPtr.h"

namespace tarkov::win32 {

/// Downloads, caches and rasterises tarkov.dev's abstract map drawings.
///
/// Renders only the region on screen, at screen resolution, rather than the
/// whole map at a fixed size: a full-map texture sharp enough for a zoomed-in
/// view would cost hundreds of megabytes. Rasterising costs tens of
/// milliseconds, so it happens on a worker thread and the previous image keeps
/// being shown, stretched, until the new one arrives.
class SvgMapStore {
public:
    using Logger = std::function<void(const std::string&)>;

    /// What the renderer should draw this frame.
    struct View {
        ID3D11ShaderResourceView* texture = nullptr;
        /// The region the texture actually covers, which lags the request
        /// while a new render is in flight. The caller positions the image by
        /// this, not by what it asked for, or the map would jump.
        map::SvgRect region;
        bool haveDocument = false;
        /// A render, or the document it needs, is on its way.
        bool rendering = false;
        /// The drawing for this map could not be downloaded or parsed. Retried
        /// at most once a minute, and only while the map is being asked for.
        bool failed = false;
    };

    ~SvgMapStore();

    SvgMapStore() = default;
    SvgMapStore(const SvgMapStore&) = delete;
    SvgMapStore& operator=(const SvgMapStore&) = delete;

    bool start(ID3D11Device* device, Logger logger = {});
    void stop();

    /// Points the store at a map. Cheap when nothing changed.
    void setMap(const std::string& mapKey, const std::string& svgUrl,
                const std::vector<std::string>& layerIds);

    /// The natural size of the loaded document, or zero while it loads.
    map::SvgSize documentSize() const;

    /// Asks for a region and returns whatever is ready. Render thread only.
    View acquire(const map::SvgRect& wanted, int width, int height);

    /// Uploads a finished render. Render thread, once per frame. Returns true
    /// when a new texture appeared.
    bool pump();

    static std::filesystem::path defaultCacheRoot();

private:
    struct Request {
        map::SvgRect region;
        int width = 0;
        int height = 0;
        std::uint64_t generation = 0;
    };

    void run();
    bool ensureDocument();
    void say(const std::string& message) const;
    std::filesystem::path cachePathFor(const std::string& mapKey) const;

    ID3D11Device* device_ = nullptr;
    Logger logger_;
    std::filesystem::path cacheRoot_;

    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::atomic<bool> stopping_{false};
    std::thread worker_;

    std::string mapKey_;
    std::string svgUrl_;
    std::vector<std::string> layerIds_;
    std::uint64_t generation_ = 0;

    /// The generation whose document could not be had, and when that was
    /// found out. Without it a missing drawing is retried in a tight loop:
    /// the wait below wakes whenever the wanted map is not the loaded one.
    std::uint64_t failedGeneration_ = ~std::uint64_t{0};
    std::chrono::steady_clock::time_point failedAt_;

    /// The worker keeps its own copy of what it is drawing, so the render
    /// thread can change the request without tearing a render in progress.
    std::string loadedKey_;
    std::vector<std::string> loadedLayers_;
    map::SvgDocument document_;
    map::SvgSize documentSize_;

    bool havePending_ = false;
    Request pending_;
    bool rendering_ = false;

    map::RenderedImage finished_;
    map::SvgRect finishedRegion_;
    std::uint64_t finishedGeneration_ = 0;
    bool haveFinished_ = false;

    ComPtr<ID3D11ShaderResourceView> texture_;
    map::SvgRect textureRegion_;
    std::uint64_t textureGeneration_ = 0;
};

}  // namespace tarkov::win32
