#include "net.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <wininet.h>
#include <mutex>
#include <vector>

static std::mutex uaMutex;
static std::wstring userAgent = L"Capsule/1.0";

static std::wstring widen(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
}

// Proxy server set by hand in Windows settings, or "". Automatic detection (WPAD/PAC) is skipped on purpose:
// it stalls every request for ~14 s on networks without a proxy.
static std::wstring manualProxy() {
    INTERNET_PER_CONN_OPTIONW opts[2]{};
    opts[0].dwOption = INTERNET_PER_CONN_FLAGS;
    opts[1].dwOption = INTERNET_PER_CONN_PROXY_SERVER;
    INTERNET_PER_CONN_OPTION_LISTW list{};
    list.dwSize = sizeof list;
    list.dwOptionCount = 2;
    list.pOptions = opts;
    DWORD size = sizeof list;
    std::wstring proxy;
    if (InternetQueryOptionW(nullptr, INTERNET_OPTION_PER_CONNECTION_OPTION, &list, &size)) {
        if ((opts[0].Value.dwValue & PROXY_TYPE_PROXY) && opts[1].Value.pszValue) proxy = opts[1].Value.pszValue;
        if (opts[1].Value.pszValue) GlobalFree(opts[1].Value.pszValue);
    }
    return proxy;
}

void setUserAgent(const std::string& ua) {
    std::lock_guard<std::mutex> lock(uaMutex);
    userAgent = widen(ua);
}

HttpResult httpRequest(const std::string& url, const std::string* post, const std::string& contentType,
                       const std::function<bool(const char*, size_t)>& sink,
                       const std::atomic<bool>* cancel, std::atomic<uint64_t>* total) {
    HttpResult r;
    std::wstring wurl = widen(url), host(256, L'\0'), path(4096, L'\0'), extra(4096, L'\0');
    URL_COMPONENTSW uc{};
    uc.dwStructSize = sizeof uc;
    uc.lpszHostName = host.data();
    uc.dwHostNameLength = (DWORD)host.size();
    uc.lpszUrlPath = path.data();
    uc.dwUrlPathLength = (DWORD)path.size();
    uc.lpszExtraInfo = extra.data(); // ?query
    uc.dwExtraInfoLength = (DWORD)extra.size();
    if (!InternetCrackUrlW(wurl.c_str(), 0, 0, &uc)) {
        r.error = "Invalid link";
        return r;
    }
    host.resize(uc.dwHostNameLength);
    path.resize(uc.dwUrlPathLength);
    path += extra.substr(0, uc.dwExtraInfoLength);

    std::wstring ua;
    {
        std::lock_guard<std::mutex> lock(uaMutex);
        ua = userAgent;
    }
    static const std::wstring proxy = manualProxy();
    HINTERNET net = InternetOpenW(ua.c_str(), proxy.empty() ? INTERNET_OPEN_TYPE_DIRECT : INTERNET_OPEN_TYPE_PROXY,
                                  proxy.empty() ? nullptr : proxy.c_str(), L"<local>", 0);
    DWORD timeout = 15000;
    if (net) {
        InternetSetOptionW(net, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof timeout);
        InternetSetOptionW(net, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof timeout);
    }
    HINTERNET conn = net ? InternetConnectW(net, host.c_str(), uc.nPort, nullptr, nullptr, INTERNET_SERVICE_HTTP, 0, 0) : nullptr;
    DWORD flags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_NO_UI;
    if (uc.nScheme == INTERNET_SCHEME_HTTPS) flags |= INTERNET_FLAG_SECURE;
    HINTERNET req = conn ? HttpOpenRequestW(conn, post ? L"POST" : L"GET", path.c_str(), nullptr, nullptr, nullptr, flags, 0) : nullptr;
    std::wstring headers = post ? L"Content-Type: " + widen(contentType) + L"\r\n" : L"";
    bool sent = req && HttpSendRequestW(req, headers.empty() ? nullptr : headers.c_str(), (DWORD)headers.size(),
                                        post ? (LPVOID)post->data() : nullptr, post ? (DWORD)post->size() : 0);
    if (!sent) {
        r.error = "Could not connect (error " + std::to_string(GetLastError()) + ")";
    } else {
        DWORD status = 0, len = sizeof status;
        HttpQueryInfoW(req, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &status, &len, nullptr);
        r.status = (int)status;
        DWORD cl = 0;
        len = sizeof cl;
        if (total && HttpQueryInfoW(req, HTTP_QUERY_CONTENT_LENGTH | HTTP_QUERY_FLAG_NUMBER, &cl, &len, nullptr)) *total = cl;
        std::vector<char> buf(1 << 16);
        DWORD n = 0;
        while (!(cancel && *cancel)) {
            if (!InternetReadFile(req, buf.data(), (DWORD)buf.size(), &n)) {
                r.error = "Download interrupted";
                break;
            }
            if (!n) break;
            if (!sink(buf.data(), n)) {
                r.error = "Download stopped";
                break;
            }
        }
        if (cancel && *cancel) r.error = "Download cancelled";
    }
    if (req) InternetCloseHandle(req);
    if (conn) InternetCloseHandle(conn);
    if (net) InternetCloseHandle(net);
    return r;
}
