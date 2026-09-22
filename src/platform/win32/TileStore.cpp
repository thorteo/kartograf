#include "platform/win32/TileStore.h"

#include <algorithm>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#include <stb/stb_image.h>

#include "platform/win32/Files.h"
#include "platform/win32/Textures.h"

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

}  // namespace

TileStore::~TileStore() { stop(); }

bool TileStore::start(ID3D11Device* device, int workerCount) {
    if (device == nullptr) {
        return false;
    }
    device_ = device;
    cacheRoot_ = appDataPath(L"tiles");

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
    {
        // Under the lock: set between a worker's check and its wait, the flag
        // would be missed and the notification lost with it, and join() would
        // wait forever.
        const std::lock_guard<std::mutex> lock(mutex_);
        stopping_.store(true, std::memory_order_relaxed);
    }
    work_.notify_all();
    cancel_.cancel();  // Downloads in flight end now, not at their timeouts.
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
        it = entries_.emplace(key, Entry{}).first;
        it->second.lastUsedFrame = frame_;
        pending_.push_back({key, mapGeneration_});
        work_.notify_one();
        return {nullptr, true};
    }

    Entry& entry = it->second;
    entry.lastUsedFrame = frame_;
    switch (entry.state) {
        case State::Ready: return {entry.texture.get(), false};
        case State::Queued:
        case State::Loading: return {nullptr, true};
        case State::Failed:
            // Asked for again once the wait is over. Not reported as loading
            // in between: nothing should poll for a tile that is not coming
            // yet, and the next frame drawn after the wait picks it up.
            if (std::chrono::steady_clock::now() >= entry.retryAt) {
                entry.state = State::Queued;
                pending_.push_back({key, mapGeneration_});
                work_.notify_one();
                return {nullptr, true};
            }
            break;
        case State::Missing: break;
    }
    return {};
}

void TileStore::markFailed(Entry& entry) {
    entry.state = State::Failed;
    entry.retryAt = std::chrono::steady_clock::now() + kRetryAfter;
}

TileStore::Outcome TileStore::fetchAndDecode(HttpClient& client, const std::string& map,
                                             const Key& key, Decoded& out) {
    const fs::path cached = cachePathFor(map, key);
    std::vector<std::uint8_t> bytes = readBinaryFile(cached);

    if (bytes.empty()) {
        std::string url;
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            url = urlFor(key);
        }
        if (url.empty()) {
            return Outcome::Missing;
        }
        const auto response = client.get(url);
        if (response.status == 404) {
            return Outcome::Missing;
        }
        if (!response.ok) {
            return Outcome::Failed;
        }
        bytes = response.body;
        writeFileAtomic(cached, std::string_view(reinterpret_cast<const char*>(bytes.data()),
                                                 bytes.size()));
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(bytes.data(),
                                            static_cast<int>(bytes.size()), &width,
                                            &height, &channels, 4);
    if (pixels == nullptr) {
        // A corrupt cache entry should not poison the tile forever: dropped,
        // it is downloaded again on the retry.
        std::error_code ec;
        fs::remove(cached, ec);
        return Outcome::Failed;
    }

    out.key = key;
    out.map = map;
    out.width = width;
    out.height = height;
    out.pixels.assign(pixels, pixels + static_cast<std::size_t>(width) * height * 4);
    stbi_image_free(pixels);
    return Outcome::Ok;
}

void TileStore::workerLoop() {
    // Each worker keeps its own client: WinHTTP handles are cheap, and sharing
    // one connection across workers would serialise them.
    HttpClient client;
    client.open(&cancel_);

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
        const Outcome outcome = fetchAndDecode(client, map, key, decoded);

        {
            const std::lock_guard<std::mutex> lock(mutex_);
            if (generation != mapGeneration_) {
                continue;  // Map changed while we were fetching.
            }
            auto it = entries_.find(key);
            if (it == entries_.end()) {
                continue;
            }
            if (outcome == Outcome::Missing) {
                it->second.state = State::Missing;
                continue;
            }
            if (outcome == Outcome::Failed) {
                markFailed(it->second);
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
        const auto view =
            createRgbaTexture(device_, tile.width, tile.height, tile.pixels.data());
        if (!view) {
            const std::lock_guard<std::mutex> lock(mutex_);
            if (auto it = entries_.find(tile.key); it != entries_.end()) {
                markFailed(it->second);
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
        // On screen: drawn last frame, or just uploaded. pump() has already
        // moved frame_ on, so "last frame" is frame_ - 1.
        if (lastUsed + 1 >= frame_) {
            continue;
        }
        entries_.erase(key);
        --toDrop;
    }
}

}  // namespace tarkov::win32
