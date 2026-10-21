#include "platform/win32/Files.h"

#include <windows.h>

#include <shlobj.h>

#include <fstream>
#include <iterator>
#include <sstream>

namespace tarkov::win32 {

namespace fs = std::filesystem;

namespace {

constexpr wchar_t kAppFolder[] = L"kartograf";
constexpr wchar_t kLegacyAppFolder[] = L"tarkov-help";

fs::path localAppData() {
    fs::path root;
    PWSTR raw = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &raw))) {
        root = raw;
    }
    CoTaskMemFree(raw);  // Owed even when the call fails.
    if (root.empty()) {
        std::error_code ec;
        root = fs::temp_directory_path(ec);
    }
    return root;
}

}  // namespace

fs::path appDataPath(std::wstring_view leaf) {
    const fs::path root = localAppData();
    std::error_code ec;
    if (!fs::exists(root / kAppFolder, ec) && fs::exists(root / kLegacyAppFolder, ec)) {
        return root / kLegacyAppFolder / leaf;
    }
    return root / kAppFolder / leaf;
}

void migrateLegacyAppData() {
    const fs::path root = localAppData();
    const fs::path current = root / kAppFolder;
    const fs::path legacy = root / kLegacyAppFolder;
    std::error_code ec;
    if (fs::exists(current, ec) || !fs::exists(legacy, ec)) {
        return;
    }
    // One rename on one volume: the folder moves whole or not at all.
    MoveFileExW(legacy.c_str(), current.c_str(), 0);
}

std::string readTextFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

std::vector<std::uint8_t> readBinaryFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in),
                                     std::istreambuf_iterator<char>());
}

bool writeFileAtomic(const fs::path& path, std::string_view data) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    // Appended as a path, not through path.string(): that converts to the
    // ANSI code page and throws for a profile folder name it cannot represent.
    fs::path temporary = path;
    temporary += L".part";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) {
            return false;
        }
        out.write(data.data(), static_cast<std::streamsize>(data.size()));
        out.close();
        if (!out) {
            fs::remove(temporary, ec);
            return false;
        }
    }
    fs::rename(temporary, path, ec);  // Replaces an existing file on Windows.
    if (ec) {
        fs::remove(temporary, ec);
        return false;
    }
    return true;
}

}  // namespace tarkov::win32
