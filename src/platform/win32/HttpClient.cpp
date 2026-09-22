#include "platform/win32/HttpClient.h"

#include <winhttp.h>

#include <algorithm>

#include "platform/win32/Utf8.h"

namespace tarkov::win32 {
namespace {

struct ParsedUrl {
    std::wstring host;
    std::wstring path;
    INTERNET_PORT port = INTERNET_DEFAULT_HTTPS_PORT;
    bool secure = true;
    bool valid = false;
};

constexpr wchar_t kUserAgent[] = L"tarkov-help/0.1";

ParsedUrl parseUrl(const std::string& url) {
    ParsedUrl parsed;
    const std::wstring wide = widen(url);

    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);
    components.dwSchemeLength = static_cast<DWORD>(-1);

    if (!WinHttpCrackUrl(wide.c_str(), static_cast<DWORD>(wide.size()), 0,
                         &components)) {
        return parsed;
    }

    parsed.host.assign(components.lpszHostName, components.dwHostNameLength);
    parsed.path.assign(components.lpszUrlPath, components.dwUrlPathLength);
    if (components.dwExtraInfoLength > 0) {
        parsed.path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
    }
    parsed.port = components.nPort;
    parsed.secure = components.nScheme == INTERNET_SCHEME_HTTPS;
    parsed.valid = !parsed.host.empty();
    return parsed;
}

/// Closes a WinHTTP handle on scope exit. These are easy to leak across the
/// several early returns in a request.
struct HandleGuard {
    HINTERNET handle = nullptr;
    ~HandleGuard() {
        if (handle != nullptr) {
            WinHttpCloseHandle(handle);
        }
    }
};

}  // namespace

void HttpCancel::cancel() {
    const std::lock_guard<std::mutex> lock(mutex_);
    cancelled_ = true;
    for (void* request : active_) {
        WinHttpCloseHandle(static_cast<HINTERNET>(request));
    }
    active_.clear();
}

bool HttpCancel::cancelled() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return cancelled_;
}

bool HttpCancel::enter(void* request) {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (cancelled_) {
        return false;
    }
    active_.push_back(request);
    return true;
}

bool HttpCancel::leave(void* request) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto it = std::find(active_.begin(), active_.end(), request);
    if (it == active_.end()) {
        return false;  // cancel() closed it
    }
    active_.erase(it);
    return true;
}

HttpClient::~HttpClient() { close(); }

bool HttpClient::open(HttpCancel* cancel) {
    close();
    cancel_ = cancel;
    session_ = WinHttpOpen(kUserAgent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                           WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session_ == nullptr) {
        return false;
    }
    // Keep a stalled request from pinning a worker thread indefinitely.
    WinHttpSetTimeouts(static_cast<HINTERNET>(session_), 5000, 5000, 15000, 15000);
    // Ask for gzip and let WinHTTP undo it. The JSON documents shrink about
    // ninefold: tasks and maps are 10.7 MB as sent plainly, 1.15 MB gzipped
    // (measured 2026-09-22). Images are sent as they are either way.
    DWORD decompression = WINHTTP_DECOMPRESSION_FLAG_ALL;
    WinHttpSetOption(static_cast<HINTERNET>(session_), WINHTTP_OPTION_DECOMPRESSION,
                     &decompression, sizeof(decompression));
    return true;
}

void HttpClient::close() {
    if (session_ != nullptr) {
        WinHttpCloseHandle(static_cast<HINTERNET>(session_));
        session_ = nullptr;
    }
}

HttpClient::Response HttpClient::get(const std::string& url, std::size_t maxBytes) {
    Response response;
    if (session_ == nullptr) {
        return response;
    }
    HINTERNET session = static_cast<HINTERNET>(session_);

    const ParsedUrl parsed = parseUrl(url);
    if (!parsed.valid) {
        return response;
    }

    HandleGuard connection{
        WinHttpConnect(session, parsed.host.c_str(), parsed.port, 0)};
    if (connection.handle == nullptr) {
        return response;
    }

    HINTERNET handle = WinHttpOpenRequest(
        connection.handle, L"GET", parsed.path.c_str(), nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, parsed.secure ? WINHTTP_FLAG_SECURE : 0);
    if (handle == nullptr) {
        return response;
    }
    if (cancel_ != nullptr && !cancel_->enter(handle)) {
        WinHttpCloseHandle(handle);
        return response;
    }
    // Closed on the way out, unless cancel() got there first. Local, so it
    // shares this function's access to HttpCancel.
    struct RequestGuard {
        HttpCancel* cancel;
        HINTERNET handle;
        ~RequestGuard() {
            if (cancel == nullptr || cancel->leave(handle)) {
                WinHttpCloseHandle(handle);
            }
        }
    } const request{cancel_, handle};

    if (!WinHttpSendRequest(request.handle, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.handle, nullptr)) {
        return response;
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    WinHttpQueryHeaders(request.handle,
                        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
                        WINHTTP_NO_HEADER_INDEX);
    response.status = status;

    for (;;) {
        if (cancel_ != nullptr && cancel_->cancelled()) {
            return response;
        }
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.handle, &available)) {
            // A dropped connection or a cancel, not the end of the body: a
            // truncated document must not come back as ok and be cached.
            return response;
        }
        if (available == 0) {
            break;
        }
        if (response.body.size() + available > maxBytes) {
            return response;  // Refuse to grow without bound.
        }
        const std::size_t offset = response.body.size();
        response.body.resize(offset + available);

        DWORD read = 0;
        if (!WinHttpReadData(request.handle, response.body.data() + offset, available,
                             &read)) {
            return response;
        }
        response.body.resize(offset + read);
        if (read == 0) {
            break;
        }
    }

    response.ok = status == 200 && !response.body.empty();
    return response;
}

}  // namespace tarkov::win32
