#pragma once

#include <graphiti/error.h>

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
        int timeout_seconds = 60
    );

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace graphiti
