#pragma once

#include <windows.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <d3d11.h>

#include "core/map/SvgRenderer.h"
#include "platform/win32/ComPtr.h"

namespace tarkov::win32 {

/// Map marker icons, fetched at runtime and cached.
///
/// The icons are the ones tarkov.dev's interactive map uses, which its history
/// records as taken from the Escape from Tarkov Wiki (CC BY-SA). They are not
/// bundled with this program: like map tiles, they are downloaded from their
/// published location and cached, and the README credits the source.
class IconStore {
public:
    ~IconStore();

    IconStore() = default;
    IconStore(const IconStore&) = delete;
    IconStore& operator=(const IconStore&) = delete;

    /// Starts fetching the named icons, e.g. "quest_objective".
    bool start(ID3D11Device* device, std::vector<std::string> names);
    void stop();

    /// Adds an icon made here rather than downloaded, uploaded on the next
    /// pump. An empty image is ignored.
    void add(const std::string& name, const map::RenderedImage& image);

    /// The texture for an icon, or nullptr until it has arrived.
    ID3D11ShaderResourceView* get(const std::string& name) const;

    /// Uploads finished icons. Render thread. Returns true if any appeared.
    bool pump();

    static std::string urlFor(const std::string& name);

private:
    struct Decoded {
        std::string name;
        int width = 0;
        int height = 0;
        std::vector<std::uint8_t> pixels;
    };

    void run(std::vector<std::string> names);

    ID3D11Device* device_ = nullptr;
    std::filesystem::path cacheRoot_;
    std::thread worker_;
    std::atomic<bool> stopping_{false};

    mutable std::mutex mutex_;
    std::deque<Decoded> decoded_;
    std::map<std::string, ComPtr<ID3D11ShaderResourceView>> textures_;
};

}  // namespace tarkov::win32
