#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace tarkov::win32 {

/// A small blocking HTTPS client built on WinHTTP.
///
/// WinHTTP rather than a vendored library: it is part of Windows, so the
/// overlay keeps its single-binary, no-dependency distribution.
///
/// One instance owns one WinHTTP session and is safe to share between threads
/// only in the sense that each call opens its own connection. Give each worker
/// thread its own client.
class HttpClient {
public:
    HttpClient() = default;
    ~HttpClient();

    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    bool open(const std::wstring& userAgent);
    void close();

    struct Response {
        bool ok = false;
        unsigned status = 0;
        std::vector<std::uint8_t> body;
    };

    /// GET the URL. `maxBytes` caps the response so a misconfigured host cannot
    /// make us allocate without limit.
    Response get(const std::string& url, std::size_t maxBytes = 8u * 1024 * 1024);

private:
    // A WinHTTP HINTERNET, kept opaque so <winhttp.h> does not leak into every
    // translation unit that only wants to make a request.
    void* session_ = nullptr;
};

}  // namespace tarkov::win32
