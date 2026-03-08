#include "http_client.h"

#define CPPHTTPLIB_OPENSSL_SUPPORT
#include <httplib.h>

#include <format>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace graphiti {

struct HttpClient::Impl {
    std::mutex mutex;
    std::unordered_map<std::string, std::unique_ptr<httplib::Client>> clients;

    httplib::Client* get_or_create(const std::string& host) {
        std::lock_guard lock(mutex);
        auto it = clients.find(host);
        if (it != clients.end()) return it->second.get();
        auto client = std::make_unique<httplib::Client>(host);
        client->set_follow_location(true);
        auto* ptr = client.get();
        clients.emplace(host, std::move(client));
        return ptr;
    }
};

HttpClient::~HttpClient() { delete impl_; }

HttpClient::HttpClient(HttpClient&& other) noexcept : impl_(other.impl_) {
    other.impl_ = nullptr;
}

HttpClient& HttpClient::operator=(HttpClient&& other) noexcept {
    if (this != &other) {
        delete impl_;
        impl_ = other.impl_;
        other.impl_ = nullptr;
    }
    return *this;
}

HttpClient::Impl* HttpClient::get_or_create(const std::string& host) {
    if (!impl_) impl_ = new Impl();
    return impl_;
}

Result<HttpResponse> HttpClient::post_json(
    const std::string& host,
    const std::string& path,
    const std::map<std::string, std::string>& headers,
    const std::string& json_body,
    int timeout_seconds
) {
    auto* pimpl = get_or_create(host);
    auto* client = pimpl->get_or_create(host);

    client->set_read_timeout(timeout_seconds, 0);
    client->set_write_timeout(timeout_seconds, 0);
    client->set_connection_timeout(timeout_seconds, 0);

    httplib::Headers h;
    for (auto& [k, v] : headers) {
        h.emplace(k, v);
    }

    auto result = client->Post(path, h, json_body, "application/json");

    if (!result) {
        auto err = result.error();
        return std::unexpected(GraphitiError{
            ErrorCode::http_error,
            std::format("HTTP request failed: {} (host: {}{})",
                httplib::to_string(err), host, path)
        });
    }

    return HttpResponse{result->status, result->body};
}

} // namespace graphiti
