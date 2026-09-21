#include "platform/win32/HttpClient.h"

#include <winhttp.h>

#include <memory>

namespace tarkov::win32 {
namespace {

struct ParsedUrl {
    std::wstring host;
    std::wstring path;
    INTERNET_PORT port = INTERNET_DEFAULT_HTTPS_PORT;
    bool secure = true;
    bool valid = false;
};

std::wstring widen(const std::string& narrow) {
    if (narrow.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, narrow.data(),
                                         static_cast<int>(narrow.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, narrow.data(), static_cast<int>(narrow.size()),
                        out.data(), size);
    return out;
}

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

HttpClient::~HttpClient() { close(); }

bool HttpClient::open(const std::wstring& userAgent) {
    close();
    session_ = WinHttpOpen(userAgent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                           WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session_ == nullptr) {
        return false;
    }
    // Keep a stalled request from pinning a worker thread indefinitely.
    WinHttpSetTimeouts(static_cast<HINTERNET>(session_), 5000, 5000, 15000, 15000);
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

    HandleGuard request{WinHttpOpenRequest(
        connection.handle, L"GET", parsed.path.c_str(), nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        parsed.secure ? WINHTTP_FLAG_SECURE : 0)};
    if (request.handle == nullptr) {
        return response;
    }

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
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.handle, &available) || available == 0) {
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
