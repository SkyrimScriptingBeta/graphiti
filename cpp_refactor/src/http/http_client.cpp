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

    // Must be called with mutex already held
    httplib::Client* get_or_create_locked(const std::string& host) {
        auto it = clients.find(host);
        if (it != clients.end()) return it->second.get();
        auto client = std::make_unique<httplib::Client>(host);
        client->set_follow_location(true);
        auto* ptr = client.get();
        clients.emplace(host, std::move(client));
        return ptr;
    }
};

HttpClient::HttpClient() : impl_(new Impl()) {}

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

Result<HttpResponse> HttpClient::post_json(
    const std::string& host,
    const std::string& path,
    const std::map<std::string, std::string>& headers,
    const std::string& json_body,
    int timeout_seconds
) {
    // Hold the Impl mutex for the entire HTTP call to prevent
    // concurrent use of the same httplib::Client (not thread-safe).
    std::lock_guard lock(impl_->mutex);
    auto* client = impl_->get_or_create_locked(host);

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
