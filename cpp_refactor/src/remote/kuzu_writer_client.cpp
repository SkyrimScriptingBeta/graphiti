#include "kuzu_writer_client.h"

#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <string>
#include <unordered_map>

using json = nlohmann::json;

namespace graphiti {

// Helper: serialize TimePoint to ISO 8601 string (ms precision)
static std::string timepoint_to_string(TimePoint tp) {
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()).count();
    return std::to_string(ms);
}

struct KuzuWriterClient::Impl {
    std::string uri;
    std::string target_db;
    ix::WebSocket ws;
    std::atomic<uint64_t> next_id{1};

    // Pending requests: id -> {response json, fulfilled flag}
    struct PendingRequest {
        json response;
        bool fulfilled = false;
    };
    std::mutex pending_mu;
    std::condition_variable pending_cv;
    std::unordered_map<std::string, PendingRequest> pending;

    std::atomic<bool> connected{false};
    std::mutex connect_mu;
    std::condition_variable connect_cv;

    Impl(std::string uri_, std::string target_db_)
        : uri(std::move(uri_)), target_db(std::move(target_db_)) {}

    // Send a JSON-RPC request and wait for the response. Returns the "result" field.
    Result<json> call(const std::string& method, json params) {
        params["target_db"] = target_db;

        auto id = std::to_string(next_id.fetch_add(1));
        json request = {
            {"jsonrpc", "2.0"},
            {"id", id},
            {"method", method},
            {"params", params}
        };

        // Register pending request before sending
        {
            std::lock_guard lock(pending_mu);
            pending[id] = PendingRequest{};
        }

        auto msg = request.dump();
        ws.send(msg);

        // Wait for response
        json response;
        {
            std::unique_lock lock(pending_mu);
            pending_cv.wait(lock, [&] { return pending[id].fulfilled; });
            response = std::move(pending[id].response);
            pending.erase(id);
        }

        // Check for JSON-RPC error
        if (response.contains("error")) {
            auto& err = response["error"];
            return std::unexpected(GraphitiError{
                ErrorCode::db_error,
                err.value("message", "unknown remote error")
            });
        }

        return response.value("result", json::object());
    }

    // Fire-and-forget write call (still waits for ACK)
    VoidResult write_call(const std::string& method, json params) {
        auto result = call(method, std::move(params));
        if (!result.has_value()) return std::unexpected(result.error());
        return {};
    }
};

KuzuWriterClient::KuzuWriterClient(std::string uri, std::string target_db)
    : impl_(std::make_unique<Impl>(std::move(uri), std::move(target_db))) {}

KuzuWriterClient::~KuzuWriterClient() {
    impl_->ws.stop();
}

VoidResult KuzuWriterClient::connect() {
    ix::initNetSystem();

    impl_->ws.setUrl(impl_->uri);
    impl_->ws.disableAutomaticReconnection();

    impl_->ws.setOnMessageCallback([this](const ix::WebSocketMessagePtr& msg) {
        if (msg->type == ix::WebSocketMessageType::Message) {
            auto response = json::parse(msg->str, nullptr, false);
            if (response.is_discarded() || !response.contains("id")) return;

            if (response["id"].is_null()) return;
            auto id = response["id"].get<std::string>();
            std::lock_guard lock(impl_->pending_mu);
            if (auto it = impl_->pending.find(id); it != impl_->pending.end()) {
                it->second.response = std::move(response);
                it->second.fulfilled = true;
                impl_->pending_cv.notify_all();
            }
        } else if (msg->type == ix::WebSocketMessageType::Open) {
            impl_->connected = true;
            impl_->connect_cv.notify_all();
        } else if (msg->type == ix::WebSocketMessageType::Error) {
            fprintf(stderr, "[kuzu-writer-client] WebSocket error: %s\n", msg->errorInfo.reason.c_str());
            // Fulfill all pending requests with error so they don't hang
            std::lock_guard lock(impl_->pending_mu);
            for (auto& [id, req] : impl_->pending) {
                if (!req.fulfilled) {
                    req.response = {{"error", {{"message", "WebSocket connection error: " + msg->errorInfo.reason}}}};
                    req.fulfilled = true;
                }
            }
            impl_->pending_cv.notify_all();
        }
    });

    impl_->ws.start();

    // Wait for connection (up to 10s)
    {
        std::unique_lock lock(impl_->connect_mu);
        if (!impl_->connect_cv.wait_for(lock, std::chrono::seconds(10), [this] { return impl_->connected.load(); })) {
            return std::unexpected(GraphitiError{ErrorCode::http_error,
                "Failed to connect to kuzu-writer-server at " + impl_->uri + " within 10s"});
        }
    }

    fprintf(stderr, "[kuzu-writer-client] Connected to %s (target_db=%s)\n",
            impl_->uri.c_str(), impl_->target_db.c_str());
    return {};
}

// --- Write methods ---

VoidResult KuzuWriterClient::save_entity_node(const EntityNode& node) {
    return impl_->write_call("save_entity_node", {{"node", node}});
}

VoidResult KuzuWriterClient::save_entity_node_embedding(std::string_view uuid, const std::vector<float>& embedding) {
    return impl_->write_call("save_entity_node_embedding", {
        {"uuid", std::string(uuid)},
        {"embedding", embedding}
    });
}

VoidResult KuzuWriterClient::save_entity_edge(const EntityEdge& edge) {
    return impl_->write_call("save_entity_edge", {{"edge", edge}});
}

VoidResult KuzuWriterClient::save_entity_edge_embedding(std::string_view uuid, const std::vector<float>& embedding) {
    return impl_->write_call("save_entity_edge_embedding", {
        {"uuid", std::string(uuid)},
        {"embedding", embedding}
    });
}

VoidResult KuzuWriterClient::save_episodic_node(const EpisodicNode& node) {
    return impl_->write_call("save_episodic_node", {{"node", node}});
}

VoidResult KuzuWriterClient::save_episodic_edge(const EpisodicEdge& edge) {
    return impl_->write_call("save_episodic_edge", {{"edge", edge}});
}

VoidResult KuzuWriterClient::save_saga_node(const SagaNode& node) {
    return impl_->write_call("save_saga_node", {{"node", node}});
}

VoidResult KuzuWriterClient::save_has_episode_edge(
    std::string_view uuid, std::string_view saga_uuid,
    std::string_view episode_uuid, std::string_view group_id,
    TimePoint created_at
) {
    return impl_->write_call("save_has_episode_edge", {
        {"uuid", std::string(uuid)},
        {"saga_uuid", std::string(saga_uuid)},
        {"episode_uuid", std::string(episode_uuid)},
        {"group_id", std::string(group_id)},
        {"created_at", timepoint_to_string(created_at)}
    });
}

VoidResult KuzuWriterClient::save_next_episode_edge(
    std::string_view uuid, std::string_view source_episode_uuid,
    std::string_view target_episode_uuid, std::string_view group_id,
    TimePoint created_at
) {
    return impl_->write_call("save_next_episode_edge", {
        {"uuid", std::string(uuid)},
        {"source_episode_uuid", std::string(source_episode_uuid)},
        {"target_episode_uuid", std::string(target_episode_uuid)},
        {"group_id", std::string(group_id)},
        {"created_at", timepoint_to_string(created_at)}
    });
}

// --- Read methods (through daemon for write-locked consistency) ---

Result<EntityNode> KuzuWriterClient::get_entity_node(std::string_view uuid) {
    auto result = impl_->call("get_entity_node", {{"uuid", std::string(uuid)}});
    if (!result.has_value()) return std::unexpected(result.error());

    auto& r = result.value();
    if (!r.contains("node") || r["node"].is_null()) {
        return std::unexpected(GraphitiError{ErrorCode::not_found, "Entity node not found: " + std::string(uuid)});
    }
    return r["node"].get<EntityNode>();
}

Result<std::optional<SagaNode>> KuzuWriterClient::get_saga_by_name(std::string_view name, std::string_view group_id) {
    auto result = impl_->call("get_saga_by_name", {
        {"name", std::string(name)},
        {"group_id", std::string(group_id)}
    });
    if (!result.has_value()) return std::unexpected(result.error());

    auto& r = result.value();
    if (!r.contains("node") || r["node"].is_null()) {
        return std::optional<SagaNode>{};
    }
    return std::optional<SagaNode>{r["node"].get<SagaNode>()};
}

Result<std::optional<std::string>> KuzuWriterClient::get_last_episode_in_saga(
    std::string_view saga_uuid, std::string_view exclude_episode_uuid
) {
    auto result = impl_->call("get_last_episode_in_saga", {
        {"saga_uuid", std::string(saga_uuid)},
        {"exclude_episode_uuid", std::string(exclude_episode_uuid)}
    });
    if (!result.has_value()) return std::unexpected(result.error());

    auto& r = result.value();
    if (!r.contains("episode_uuid") || r["episode_uuid"].is_null()) {
        return std::optional<std::string>{};
    }
    return std::optional<std::string>{r["episode_uuid"].get<std::string>()};
}

} // namespace graphiti
