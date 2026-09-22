#include "platform/win32/TileStore.h"

#include <shlobj.h>

#include <algorithm>
#include <fstream>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#include <stb/stb_image.h>

namespace tarkov::win32 {
namespace {

namespace fs = std::filesystem;

std::string replaceAll(std::string text, const std::string& from,
                       const std::string& to) {
    std::size_t at = 0;
    while ((at = text.find(from, at)) != std::string::npos) {
        text.replace(at, from.size(), to);
        at += to.size();
    }
    return text;
}

std::vector<std::uint8_t> readFileBytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in),
                                     std::istreambuf_iterator<char>());
}

bool writeFileBytes(const fs::path& path, const std::vector<std::uint8_t>& bytes) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    // Write to a temporary and rename, so a crash mid-write cannot leave a
    // truncated PNG in the cache to be served forever afterwards.
    const fs::path temporary = path.string() + ".part";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) {
            return false;
        }
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
        if (!out) {
            return false;
        }
    }
    fs::rename(temporary, path, ec);
    if (ec) {
        fs::remove(temporary, ec);
        return false;
    }
    return true;
}

}  // namespace

TileStore::~TileStore() { stop(); }

fs::path TileStore::defaultCacheRoot() {
    PWSTR raw = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &raw))) {
        fs::path path(raw);
        CoTaskMemFree(raw);
        return path / "tarkov-help" / "tiles";
    }
    return fs::temp_directory_path() / "tarkov-help" / "tiles";
}

bool TileStore::start(ID3D11Device* device, int workerCount) {
    if (device == nullptr) {
        return false;
    }
    device_ = device;
    cacheRoot_ = defaultCacheRoot();

    std::error_code ec;
    fs::create_directories(cacheRoot_, ec);

    stopping_.store(false, std::memory_order_relaxed);
    workers_.reserve(static_cast<std::size_t>(workerCount));
    for (int i = 0; i < workerCount; ++i) {
        workers_.emplace_back(&TileStore::workerLoop, this);
    }
    return true;
}

void TileStore::stop() {
    if (workers_.empty()) {
        return;
    }
    stopping_.store(true, std::memory_order_relaxed);
    work_.notify_all();
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers_.clear();

    const std::lock_guard<std::mutex> lock(mutex_);
    entries_.clear();
    pending_.clear();
    decoded_.clear();
}

void TileStore::setMap(const std::string& normalizedName,
                       const std::string& groundTemplate,
                       const std::vector<std::string>& layerTemplates) {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (mapName_ == normalizedName && tileTemplate_ == groundTemplate &&
        layerTemplates_ == layerTemplates) {
        return;
    }
    mapName_ = normalizedName;
    tileTemplate_ = groundTemplate;
    layerTemplates_ = layerTemplates;
    // Bumping the generation makes every in-flight request stale: a worker that
    // finishes a tile for the previous map discards it rather than uploading
    // the wrong picture.
    ++mapGeneration_;
    entries_.clear();
    pending_.clear();
    decoded_.clear();
}

const std::string* TileStore::templateFor(int layer) const {
    if (layer < 0) {
        return tileTemplate_.empty() ? nullptr : &tileTemplate_;
    }
    if (static_cast<std::size_t>(layer) >= layerTemplates_.size()) {
        return nullptr;
    }
    const std::string& candidate = layerTemplates_[static_cast<std::size_t>(layer)];
    return candidate.empty() ? nullptr : &candidate;
}

std::string TileStore::urlFor(const Key& key) const {
    const std::string* pattern = templateFor(key.layer);
    if (pattern == nullptr) {
        return {};
    }
    std::string url = *pattern;
    url = replaceAll(url, "{z}", std::to_string(key.zoom));
    url = replaceAll(url, "{x}", std::to_string(key.x));
    url = replaceAll(url, "{y}", std::to_string(key.y));
    return url;
}

fs::path TileStore::cachePathFor(const std::string& map, const Key& key) const {
    // Ground and each upper floor get their own subtree, so the same z/x/y from
    // two floors cannot collide.
    const std::string floor =
        key.layer < 0 ? std::string("ground") : ("layer" + std::to_string(key.layer));
    return cacheRoot_ / map / floor / std::to_string(key.zoom) /
           std::to_string(key.x) / (std::to_string(key.y) + ".png");
}

TileStore::Lookup TileStore::acquire(const Key& key) {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (tileTemplate_.empty()) {
        return {};
    }

    if (templateFor(key.layer) == nullptr) {
        return {};  // This floor publishes no tiles.
    }

    auto it = entries_.find(key);
    if (it == entries_.end()) {
        entries_[key] = Entry{State::Queued, {}, frame_};
        pending_.push_back({key, mapGeneration_});
        work_.notify_one();
        return {nullptr, true};
    }

    it->second.lastUsedFrame = frame_;
    switch (it->second.state) {
        case State::Ready: return {it->second.texture.get(), false};
        case State::Queued:
        case State::Loading: return {nullptr, true};
        case State::Failed: break;
    }
    return {};
}

bool TileStore::fetchAndDecode(const std::string& map, const Key& key, Decoded& out) {
    // Each worker keeps its own client: WinHTTP handles are cheap, and sharing
    // one connection across workers would serialise them.
    thread_local HttpClient client;
    thread_local bool opened = false;
    if (!opened) {
        opened = client.open(L"tarkov-help/0.1 (+https://github.com/)");
    }

    const fs::path cached = cachePathFor(map, key);
    std::vector<std::uint8_t> bytes = readFileBytes(cached);

    if (bytes.empty()) {
        if (!opened) {
            return false;
        }
        std::string url;
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            url = urlFor(key);
        }
        if (url.empty()) {
            return false;
        }
        const auto response = client.get(url);
        if (!response.ok) {
            return false;
        }
        bytes = response.body;
        writeFileBytes(cached, bytes);
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(bytes.data(),
                                            static_cast<int>(bytes.size()), &width,
                                            &height, &channels, 4);
    if (pixels == nullptr) {
        // A corrupt cache entry should not poison the tile forever.
        std::error_code ec;
        fs::remove(cached, ec);
        return false;
    }

    out.key = key;
    out.map = map;
    out.width = width;
    out.height = height;
    out.pixels.assign(pixels, pixels + static_cast<std::size_t>(width) * height * 4);
    stbi_image_free(pixels);
    return true;
}

void TileStore::workerLoop() {
    for (;;) {
        Key key;
        std::string map;
        std::uint64_t generation = 0;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            work_.wait(lock, [this] {
                return stopping_.load(std::memory_order_relaxed) || !pending_.empty();
            });
            if (stopping_.load(std::memory_order_relaxed)) {
                return;
            }
            // Take the most recent request: the player is looking at what they
            // just panned to, not at what they asked for ten moves ago.
            auto request = pending_.back();
            pending_.pop_back();
            key = request.first;
            generation = request.second;
            if (generation != mapGeneration_) {
                continue;
            }
            map = mapName_;
            auto it = entries_.find(key);
            if (it == entries_.end()) {
                continue;
            }
            it->second.state = State::Loading;
        }

        Decoded decoded;
        const bool ok = fetchAndDecode(map, key, decoded);

        {
            const std::lock_guard<std::mutex> lock(mutex_);
            if (generation != mapGeneration_) {
                continue;  // Map changed while we were fetching.
            }
            auto it = entries_.find(key);
            if (it == entries_.end()) {
                continue;
            }
            if (!ok) {
                it->second.state = State::Failed;
                continue;
            }
            decoded_.push_back(std::move(decoded));
        }
    }
}

int TileStore::pump(int maxUploads) {
    std::vector<Decoded> batch;
    std::uint64_t generation = 0;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        ++frame_;
        generation = mapGeneration_;
        while (!decoded_.empty() && static_cast<int>(batch.size()) < maxUploads) {
            batch.push_back(std::move(decoded_.front()));
            decoded_.pop_front();
        }
    }
    if (batch.empty()) {
        return 0;
    }

    int uploaded = 0;
    for (auto& tile : batch) {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = static_cast<UINT>(tile.width);
        desc.Height = static_cast<UINT>(tile.height);
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA initial{};
        initial.pSysMem = tile.pixels.data();
        initial.SysMemPitch = static_cast<UINT>(tile.width * 4);

        ComPtr<ID3D11Texture2D> texture;
        if (FAILED(device_->CreateTexture2D(&desc, &initial, texture.put()))) {
            const std::lock_guard<std::mutex> lock(mutex_);
            if (auto it = entries_.find(tile.key); it != entries_.end()) {
                it->second.state = State::Failed;
            }
            continue;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc{};
        viewDesc.Format = desc.Format;
        viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        viewDesc.Texture2D.MipLevels = 1;

        ComPtr<ID3D11ShaderResourceView> view;
        if (FAILED(device_->CreateShaderResourceView(texture.get(), &viewDesc,
                                                     view.put()))) {
            const std::lock_guard<std::mutex> lock(mutex_);
            if (auto it = entries_.find(tile.key); it != entries_.end()) {
                it->second.state = State::Failed;
            }
            continue;
        }

        const std::lock_guard<std::mutex> lock(mutex_);
        if (generation != mapGeneration_) {
            continue;
        }
        auto it = entries_.find(tile.key);
        if (it == entries_.end()) {
            continue;
        }
        it->second.texture = view;
        it->second.state = State::Ready;
        it->second.lastUsedFrame = frame_;
        ++uploaded;
    }

    if (uploaded > 0) {
        const std::lock_guard<std::mutex> lock(mutex_);
        evictIfNeeded();
    }
    return uploaded;
}

void TileStore::evictIfNeeded() {
    // Caller holds the lock.
    std::size_t ready = 0;
    for (const auto& [key, entry] : entries_) {
        if (entry.state == State::Ready) {
            ++ready;
        }
    }
    if (ready <= kMaxTextures) {
        return;
    }

    std::vector<std::pair<std::uint64_t, Key>> candidates;
    candidates.reserve(ready);
    for (const auto& [key, entry] : entries_) {
        if (entry.state == State::Ready) {
            candidates.push_back({entry.lastUsedFrame, key});
        }
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    // Drop the least recently drawn. The disk cache keeps the bytes, so
    // bringing one back costs a decode rather than a download.
    std::size_t toDrop = ready - kMaxTextures;
    for (const auto& [lastUsed, key] : candidates) {
        if (toDrop == 0) {
            break;
        }
        if (lastUsed == frame_) {
            continue;  // On screen right now.
        }
        entries_.erase(key);
        --toDrop;
    }
}

TileStore::Stats TileStore::stats() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    Stats stats;
    for (const auto& [key, entry] : entries_) {
        switch (entry.state) {
            case State::Ready: ++stats.ready; break;
            case State::Loading: ++stats.loading; break;
            case State::Failed: ++stats.failed; break;
            case State::Queued: ++stats.queued; break;
        }
    }
    return stats;
}

}  // namespace tarkov::win32
