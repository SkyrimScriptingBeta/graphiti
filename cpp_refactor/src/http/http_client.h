#pragma once

#include <graphiti/error.h>

#include <functional>
#include <map>
#include <string>

namespace graphiti {

struct HttpResponse {
    int status;
    std::string body;
};

class HttpClient {
public:
    HttpClient();
    ~HttpClient();

    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;
    HttpClient(HttpClient&&) noexcept;
    HttpClient& operator=(HttpClient&&) noexcept;

    Result<HttpResponse> post_json(
        const std::string& host,
        const std::string& path,
        const std::map<std::string, std::string>& headers,
        const std::string& json_body,
        int timeout_seconds = 300  // 5 min — local models on large prompts need time
    );

    // Same as post_json but with SSE streaming. Calls on_token for each content delta.
    // Accumulates the full response body and returns it as a non-streaming HttpResponse.
    using TokenCallback = std::function<void(const std::string& token)>;
    Result<HttpResponse> post_json_streaming(
        const std::string& host,
        const std::string& path,
        const std::map<std::string, std::string>& headers,
        const std::string& json_body,
        TokenCallback on_token,
        int timeout_seconds = 300
    );

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace graphiti
