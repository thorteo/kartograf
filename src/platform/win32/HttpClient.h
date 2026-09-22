#pragma once

#include <windows.h>

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace tarkov::win32 {

/// Ends requests in progress on other threads, and refuses new ones.
///
/// A worker blocked in a download cannot be joined until the download ends,
/// which on a stalled connection is the whole of WinHTTP's timeouts, per
/// request. Closing the request handle is WinHTTP's documented way to end a
/// synchronous request early: the blocked call returns with an error. So each
/// request registers its handle here while it runs, and cancel() closes them.
///
/// One per component, shared by all of its clients.
class HttpCancel {
public:
    void cancel();
    bool cancelled() const;

private:
    friend class HttpClient;
    /// Registers a request. False when already cancelled: do not start it.
    bool enter(void* request);
    /// Unregisters a request. True when the caller still owns the handle and
    /// must close it; false when cancel() already has.
    bool leave(void* request);

    mutable std::mutex mutex_;
    bool cancelled_ = false;
    std::vector<void*> active_;
};

/// A small blocking HTTPS client built on WinHTTP.
///
/// WinHTTP rather than a vendored library: it is part of Windows, so the
/// overlay keeps its single-binary, no-dependency distribution.
///
/// One client per worker thread; each call opens its own connection.
class HttpClient {
public:
    HttpClient() = default;
    ~HttpClient();

    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    /// `cancel`, if given, must outlive the client.
    bool open(HttpCancel* cancel = nullptr);
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
    HttpCancel* cancel_ = nullptr;
};

}  // namespace tarkov::win32
