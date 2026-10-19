#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

struct HttpResult {
    int status = 0;    // HTTP status, 0 if the request never got an answer
    std::string error; // set when the transfer failed (not for non-200 answers)
};

// Blocking HTTP(S) request. post == nullptr sends a GET. The body is streamed to sink,
// which returns false to abort. total receives Content-Length when the server sends it.
HttpResult httpRequest(const std::string& url, const std::string* post, const std::string& contentType,
                       const std::function<bool(const char*, size_t)>& sink,
                       const std::atomic<bool>* cancel = nullptr, std::atomic<uint64_t>* total = nullptr);

// Appended to the User-Agent of every request (RetroAchievements asks clients to identify themselves).
void setUserAgent(const std::string& ua);
