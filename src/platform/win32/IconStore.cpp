#include "platform/win32/IconStore.h"

#include <stb/stb_image.h>

#include "platform/win32/Files.h"
#include "platform/win32/Textures.h"

namespace tarkov::win32 {
namespace {

namespace fs = std::filesystem;

/// How long to wait before trying icons that could not be had again. Offline
/// at startup, they once never arrived until the overlay was restarted; the
/// vector stand-ins are drawn in the meantime.
constexpr auto kRetryAfter = std::chrono::minutes(5);

}  // namespace

IconStore::~IconStore() { stop(); }

std::string IconStore::urlFor(const std::string& name) {
    return "https://tarkov.dev/maps/interactive/" + name + ".png";
}

bool IconStore::start(ID3D11Device* device, std::vector<std::string> names) {
    if (device == nullptr) {
        return false;
    }
    device_ = device;
    cacheRoot_ = appDataPath(L"icons");
    std::error_code ec;
    fs::create_directories(cacheRoot_, ec);
    stopping_ = false;
    worker_ = std::thread(&IconStore::run, this, std::move(names));
    return true;
}

void IconStore::stop() {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    wake_.notify_all();
    cancel_.cancel();
    if (worker_.joinable()) {
        worker_.join();
    }
}

IconStore::Outcome IconStore::load(HttpClient& client, const std::string& name) {
    const fs::path cached = cacheRoot_ / (name + ".png");
    std::vector<std::uint8_t> bytes = readBinaryFile(cached);
    if (bytes.empty()) {
        const auto response = client.get(urlFor(name), 1024 * 1024);
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

    int width = 0, height = 0, channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(
        bytes.data(), static_cast<int>(bytes.size()), &width, &height, &channels, 4);
    if (pixels == nullptr) {
        std::error_code ec;
        fs::remove(cached, ec);  // Fetched afresh on the retry.
        return Outcome::Failed;
    }
    Decoded decoded;
    decoded.name = name;
    decoded.width = width;
    decoded.height = height;
    decoded.pixels.assign(pixels, pixels + static_cast<std::size_t>(width) * height * 4);
    stbi_image_free(pixels);

    const std::lock_guard<std::mutex> lock(mutex_);
    decoded_.push_back(std::move(decoded));
    return Outcome::Ok;
}

void IconStore::run(std::vector<std::string> names) {
    HttpClient client;
    client.open(&cancel_);

    for (;;) {
        std::vector<std::string> failed;
        for (const auto& name : names) {
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                if (stopping_) {
                    return;
                }
            }
            if (load(client, name) == Outcome::Failed) {
                failed.push_back(name);
            }
        }
        if (failed.empty()) {
            return;
        }
        names = std::move(failed);

        std::unique_lock<std::mutex> lock(mutex_);
        if (wake_.wait_for(lock, kRetryAfter, [this] { return stopping_; })) {
            return;
        }
    }
}

void IconStore::add(const std::string& name, const map::RenderedImage& image) {
    if (!image.valid()) {
        return;
    }
    const std::lock_guard<std::mutex> lock(mutex_);
    decoded_.push_back(Decoded{name, image.width, image.height, image.pixels});
}

ID3D11ShaderResourceView* IconStore::get(const std::string& name) const {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto it = textures_.find(name);
    return it == textures_.end() ? nullptr : it->second.get();
}

bool IconStore::pump() {
    std::deque<Decoded> batch;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        batch.swap(decoded_);
    }
    bool any = false;
    for (auto& icon : batch) {
        const auto view =
            createRgbaTexture(device_, icon.width, icon.height, icon.pixels.data());
        if (!view) {
            continue;
        }
        const std::lock_guard<std::mutex> lock(mutex_);
        textures_[icon.name] = view;
        any = true;
    }
    return any;
}

}  // namespace tarkov::win32
