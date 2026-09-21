#include "platform/win32/GameFiles.h"

#include <shlobj.h>

#include <algorithm>
#include <array>

namespace tarkov::win32 {
namespace {

namespace fs = std::filesystem;

std::optional<std::wstring> readRegistryString(HKEY root, const wchar_t* subKey,
                                               const wchar_t* value) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subKey, 0, KEY_READ | KEY_WOW64_64KEY, &key) !=
        ERROR_SUCCESS) {
        return std::nullopt;
    }

    DWORD type = 0;
    DWORD bytes = 0;
    LSTATUS status = RegQueryValueExW(key, value, nullptr, &type, nullptr, &bytes);
    if (status != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) {
        RegCloseKey(key);
        return std::nullopt;
    }

    std::wstring buffer(bytes / sizeof(wchar_t) + 1, L'\0');
    status = RegQueryValueExW(key, value, nullptr, nullptr,
                              reinterpret_cast<LPBYTE>(buffer.data()), &bytes);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS) {
        return std::nullopt;
    }
    buffer.resize(wcslen(buffer.c_str()));
    return buffer;
}

std::optional<fs::path> documentsFolder() {
    PWSTR raw = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &raw))) {
        return std::nullopt;
    }
    fs::path path(raw);
    CoTaskMemFree(raw);
    return path;
}

std::optional<fs::path> installFromRegistry() {
    // The launcher writes a standard uninstall entry.
    constexpr std::array<const wchar_t*, 2> kKeys{{
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\EscapeFromTarkov",
        L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\"
        L"EscapeFromTarkov",
    }};
    for (const wchar_t* key : kKeys) {
        if (auto value = readRegistryString(HKEY_LOCAL_MACHINE, key,
                                            L"InstallLocation")) {
            if (!value->empty() && fs::exists(*value)) {
                return fs::path(*value);
            }
        }
    }
    return std::nullopt;
}

/// The conventional install locations, on every fixed drive.
std::vector<fs::path> candidateInstalls() {
    std::vector<fs::path> candidates;
    constexpr std::array<const wchar_t*, 3> kSuffixes{{
        L"Battlestate Games\\Escape from Tarkov",
        L"Games\\Battlestate Games\\Escape from Tarkov",
        L"Program Files\\Battlestate Games\\Escape from Tarkov",
    }};

    const DWORD drives = GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if ((drives & (1u << i)) == 0) {
            continue;
        }
        wchar_t root[] = {static_cast<wchar_t>(L'A' + i), L':', L'\\', L'\0'};
        if (GetDriveTypeW(root) != DRIVE_FIXED) {
            continue;
        }
        for (const wchar_t* suffix : kSuffixes) {
            candidates.push_back(fs::path(root) / suffix);
        }
    }
    return candidates;
}

}  // namespace

std::optional<GamePaths> discoverGamePaths() {
    GamePaths paths;
    std::error_code ec;

    fs::path install;
    if (auto fromRegistry = installFromRegistry()) {
        install = *fromRegistry;
    } else {
        for (const auto& candidate : candidateInstalls()) {
            if (fs::exists(candidate / "EscapeFromTarkov.exe", ec)) {
                install = candidate;
                break;
            }
        }
    }

    if (!install.empty()) {
        const fs::path logs = install / "Logs";
        if (fs::is_directory(logs, ec)) {
            paths.logsRoot = logs;
        }
    }

    if (auto documents = documentsFolder()) {
        const fs::path shots = *documents / "Escape from Tarkov" / "Screenshots";
        if (fs::is_directory(shots, ec)) {
            paths.screenshots = shots;
        }
    }

    if (paths.logsRoot.empty() && paths.screenshots.empty()) {
        return std::nullopt;
    }
    return paths;
}

std::optional<fs::path> newestLogFolder(const fs::path& logsRoot) {
    std::error_code ec;
    if (!fs::is_directory(logsRoot, ec)) {
        return std::nullopt;
    }

    fs::path newest;
    for (const auto& entry : fs::directory_iterator(logsRoot, ec)) {
        if (!entry.is_directory(ec)) {
            continue;
        }
        const std::wstring name = entry.path().filename().wstring();
        if (name.rfind(L"log_", 0) != 0) {
            continue;
        }
        // Names start with a sortable timestamp, so comparing them compares
        // times without parsing anything.
        if (newest.empty() || name > newest.filename().wstring()) {
            newest = entry.path();
        }
    }
    if (newest.empty()) {
        return std::nullopt;
    }
    return newest;
}

LogTail::~LogTail() { close(); }

bool LogTail::open(const fs::path& path, bool fromStart) {
    close();

    // The game holds the file open for writing, so we must share everything.
    handle_ = CreateFileW(path.c_str(), GENERIC_READ,
                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                          nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle_ == INVALID_HANDLE_VALUE) {
        return false;
    }

    path_ = path;
    partial_.clear();
    if (!fromStart) {
        LARGE_INTEGER zero{};
        SetFilePointerEx(handle_, zero, nullptr, FILE_END);
    }
    return true;
}

void LogTail::close() {
    if (handle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
    }
    partial_.clear();
}

std::vector<std::string> LogTail::readNewLines() {
    std::vector<std::string> lines;
    if (handle_ == INVALID_HANDLE_VALUE) {
        return lines;
    }

    char buffer[16 * 1024];
    for (;;) {
        DWORD read = 0;
        if (!ReadFile(handle_, buffer, sizeof(buffer), &read, nullptr) || read == 0) {
            break;
        }
        partial_.append(buffer, read);

        std::size_t start = 0;
        for (;;) {
            const std::size_t newline = partial_.find('\n', start);
            if (newline == std::string::npos) {
                break;
            }
            // The trailing '\r' is left in place; the parser strips it, and
            // the fixtures keep CRLF for exactly this reason.
            lines.emplace_back(partial_, start, newline - start);
            start = newline + 1;
        }
        partial_.erase(0, start);

        if (read < sizeof(buffer)) {
            break;
        }
    }
    return lines;
}

}  // namespace tarkov::win32
