#pragma once

#include <windows.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <d3d11.h>

#include "platform/win32/ComPtr.h"
#include "platform/win32/HttpClient.h"

namespace tarkov::win32 {

/// Fetches, caches and uploads map tiles.
///
/// Tiles come from the same CDN tarkov.dev uses, are cached to disk forever
/// (they only change when the game's maps do), and are uploaded to the GPU on
/// the render thread. No game assets are bundled with the application.
///
/// Threading: worker threads do the network and PNG decoding; the render
/// thread creates textures. Decoded pixels cross between them through one
/// mutex-guarded queue.
class TileStore {
public:
    struct Key {
        /// -1 is ground level; 0 and up index MapVariant::layers.
        int layer = -1;
        int zoom = 0;
        int x = 0;
        int y = 0;
        bool operator<(const Key& other) const {
            if (layer != other.layer) return layer < other.layer;
            if (zoom != other.zoom) return zoom < other.zoom;
            if (x != other.x) return x < other.x;
            return y < other.y;
        }
    };

    ~TileStore();

    TileStore() = default;
    TileStore(const TileStore&) = delete;
    TileStore& operator=(const TileStore&) = delete;

    bool start(ID3D11Device* device, int workerCount = 3);
    void stop();

    /// Points the store at a map.
    ///
    /// `groundTemplate` is the base layer; `layerTemplates` are the upper
    /// floors, indexed to match MapVariant::layers. Keeping every floor in one
    /// store means switching floors costs nothing once a floor has been seen —
    /// re-pointing the store at each floor in turn would throw the tiles away
    /// on every step through a building.
    ///
    /// Changing maps does drop everything: tiles for a map the player has left
    /// are only taking up memory.
    void setMap(const std::string& normalizedName, const std::string& groundTemplate,
                const std::vector<std::string>& layerTemplates);

    struct Lookup {
        ID3D11ShaderResourceView* texture = nullptr;
        /// Requested and still coming. False for a tile that failed, or that
        /// the floor does not publish: nothing should wait for those.
        bool loading = false;
    };

    /// The texture for a tile, if it is ready. Requests it on first miss.
    /// Render thread only.
    Lookup acquire(const Key& key);

    /// Uploads decoded tiles to the GPU. Render thread, once per frame.
    /// Returns how many textures were created, so the caller can tell whether
    /// the frame changed.
    int pump(int maxUploads = 4);

private:
    enum class State {
        Queued,
        Loading,
        Ready,
        /// Could not be had this time -- no network, a server error, a bad
        /// download. Asked for again after kRetryAfter; once, a blip in the
        /// connection left holes in the map until the player changed maps.
        Failed,
        /// The CDN has no such tile. Never asked for again.
        Missing,
    };

    struct Entry {
        State state = State::Queued;
        ComPtr<ID3D11ShaderResourceView> texture;
        std::uint64_t lastUsedFrame = 0;
        std::chrono::steady_clock::time_point retryAt;
    };

    enum class Outcome { Ok, Missing, Failed };

    struct Decoded {
        Key key;
        std::string map;
        int width = 0;
        int height = 0;
        std::vector<std::uint8_t> pixels;  ///< RGBA8
    };

    void workerLoop();
    Outcome fetchAndDecode(HttpClient& client, const std::string& map, const Key& key,
                           Decoded& out);
    void markFailed(Entry& entry);
    std::filesystem::path cachePathFor(const std::string& map, const Key& key) const;
    std::string urlFor(const Key& key) const;
    const std::string* templateFor(int layer) const;
    void evictIfNeeded();

    ID3D11Device* device_ = nullptr;
    std::filesystem::path cacheRoot_;

    mutable std::mutex mutex_;
    std::condition_variable work_;
    std::atomic<bool> stopping_{false};
    HttpCancel cancel_;

    std::string mapName_;
    std::string tileTemplate_;
    std::vector<std::string> layerTemplates_;
    std::uint64_t mapGeneration_ = 0;

    std::map<Key, Entry> entries_;
    std::deque<std::pair<Key, std::uint64_t>> pending_;  ///< key + generation
    std::deque<Decoded> decoded_;

    std::vector<std::thread> workers_;
    std::uint64_t frame_ = 0;

    /// 256x256 RGBA is 256 KB per tile, so this cap is the memory budget.
    /// 192 tiles is about 48 MB, comfortably more than one screen at any zoom.
    static constexpr std::size_t kMaxTextures = 192;
    static constexpr std::chrono::seconds kRetryAfter{30};
};

}  // namespace tarkov::win32
