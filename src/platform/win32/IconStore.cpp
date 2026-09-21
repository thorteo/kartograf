#include "platform/win32/IconStore.h"

#include <shlobj.h>

#include <fstream>
#include <sstream>

#include <stb/stb_image.h>

#include "platform/win32/HttpClient.h"

namespace tarkov::win32 {
namespace {

namespace fs = std::filesystem;

fs::path cacheRootPath() {
    PWSTR raw = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &raw))) {
        fs::path path(raw);
        CoTaskMemFree(raw);
        return path / "tarkov-help" / "icons";
    }
    return fs::temp_directory_path() / "tarkov-help" / "icons";
}

std::vector<std::uint8_t> readBytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in),
                                     std::istreambuf_iterator<char>());
}

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
    cacheRoot_ = cacheRootPath();
    std::error_code ec;
    fs::create_directories(cacheRoot_, ec);
    stopping_.store(false, std::memory_order_relaxed);
    worker_ = std::thread(&IconStore::run, this, std::move(names));
    return true;
}

void IconStore::stop() {
    stopping_.store(true, std::memory_order_relaxed);
    if (worker_.joinable()) {
        worker_.join();
    }
}

void IconStore::run(std::vector<std::string> names) {
    HttpClient client;
    const bool online = client.open(L"tarkov-help/0.1");

    for (const auto& name : names) {
        if (stopping_.load(std::memory_order_relaxed)) {
            return;
        }
        const fs::path cached = cacheRoot_ / (name + ".png");
        std::vector<std::uint8_t> bytes = readBytes(cached);
        if (bytes.empty() && online) {
            const auto response = client.get(urlFor(name), 1024 * 1024);
            if (response.ok) {
                bytes = response.body;
                std::ofstream out(cached, std::ios::binary | std::ios::trunc);
                out.write(reinterpret_cast<const char*>(bytes.data()),
                          static_cast<std::streamsize>(bytes.size()));
            }
        }
        if (bytes.empty()) {
            continue;  // The vector fallback marker is drawn instead.
        }

        int width = 0, height = 0, channels = 0;
        stbi_uc* pixels = stbi_load_from_memory(
            bytes.data(), static_cast<int>(bytes.size()), &width, &height, &channels, 4);
        if (pixels == nullptr) {
            std::error_code ec;
            fs::remove(cached, ec);
            continue;
        }
        Decoded decoded;
        decoded.name = name;
        decoded.width = width;
        decoded.height = height;
        decoded.pixels.assign(pixels, pixels + static_cast<std::size_t>(width) * height * 4);
        stbi_image_free(pixels);

        const std::lock_guard<std::mutex> lock(mutex_);
        decoded_.push_back(std::move(decoded));
    }
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
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = static_cast<UINT>(icon.width);
        desc.Height = static_cast<UINT>(icon.height);
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA initial{};
        initial.pSysMem = icon.pixels.data();
        initial.SysMemPitch = static_cast<UINT>(icon.width * 4);

        ComPtr<ID3D11Texture2D> texture;
        if (FAILED(device_->CreateTexture2D(&desc, &initial, texture.put()))) {
            continue;
        }
        ComPtr<ID3D11ShaderResourceView> view;
        if (FAILED(device_->CreateShaderResourceView(texture.get(), nullptr, view.put()))) {
            continue;
        }
        const std::lock_guard<std::mutex> lock(mutex_);
        textures_[icon.name] = view;
        any = true;
    }
    return any;
}

}  // namespace tarkov::win32
