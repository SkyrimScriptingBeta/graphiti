// graphiti-kuzu-writer-server
//
// Centralized Kuzu write daemon. Holds exclusive write locks on all .kuzu DBs.
// Extraction processes connect over WebSocket and send JSON-RPC write requests.
// Each DB gets its own serial write queue — no lock contention.
//
// Usage:
//   graphiti-kuzu-writer-server \
//     --host 127.0.0.1 --port 9876 \
//     --kuzu keel=/path/to/keel/knowledge \
//     --kuzu wrench=/path/to/wrench/knowledge

#include "driver/kuzu_driver.h"

#include <CLI/CLI.hpp>
#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>
#include <ixwebsocket/IXWebSocketServer.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdio>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

using json = nlohmann::json;
using namespace graphiti;

// --- Signal handling ---
static std::atomic<bool> g_running{true};

static void signal_handler(int) {
    g_running = false;
}

// --- Per-DB write queue ---
struct DbQueue {
    std::string name;
    std::unique_ptr<KuzuDriver> driver;
    std::thread worker;
    std::mutex mu;
    std::condition_variable cv;
    std::deque<std::function<void()>> tasks;
    std::atomic<bool> stopping{false};

    void start() {
        worker = std::thread([this] {
            fprintf(stderr, "[kuzu-writer] Queue '%s' started\n", name.c_str());
            while (true) {
                std::function<void()> task;
                {
                    std::unique_lock lock(mu);
                    cv.wait(lock, [this] { return !tasks.empty() || stopping; });
                    if (stopping && tasks.empty()) break;
                    task = std::move(tasks.front());
                    tasks.pop_front();
                }
                task();
            }
            fprintf(stderr, "[kuzu-writer] Queue '%s' drained and stopped\n", name.c_str());
        });
    }

    void enqueue(std::function<void()> task) {
        {
            std::lock_guard lock(mu);
            tasks.push_back(std::move(task));
        }
        cv.notify_one();
    }

    void stop() {
        stopping = true;
        cv.notify_one();
        if (worker.joinable()) worker.join();
    }
};

// --- TimePoint parsing ---
static TimePoint parse_timepoint(const json& j) {
    if (j.is_number()) {
        auto ms = j.get<int64_t>();
        return TimePoint(std::chrono::milliseconds(ms));
    }
    return std::chrono::system_clock::now();
}

// --- Request dispatcher ---
static json dispatch(KuzuDriver& driver, const std::string& method, const json& params) {
    // --- Writes ---
    if (method == "save_entity_node") {
        auto node = params.at("node").get<EntityNode>();
        auto r = driver.save_entity_node(node);
        if (!r.has_value()) return {{"error", {{"code", -1}, {"message", r.error().message}}}};
        return {{"result", {{"ok", true}}}};
    }
    if (method == "save_entity_node_embedding") {
        auto uuid = params.at("uuid").get<std::string>();
        auto embedding = params.at("embedding").get<std::vector<float>>();
        auto r = driver.save_entity_node_embedding(uuid, embedding);
        if (!r.has_value()) return {{"error", {{"code", -1}, {"message", r.error().message}}}};
        return {{"result", {{"ok", true}}}};
    }
    if (method == "save_entity_edge") {
        auto edge = params.at("edge").get<EntityEdge>();
        auto r = driver.save_entity_edge(edge);
        if (!r.has_value()) return {{"error", {{"code", -1}, {"message", r.error().message}}}};
        return {{"result", {{"ok", true}}}};
    }
    if (method == "save_entity_edge_embedding") {
        auto uuid = params.at("uuid").get<std::string>();
        auto embedding = params.at("embedding").get<std::vector<float>>();
        auto r = driver.save_entity_edge_embedding(uuid, embedding);
        if (!r.has_value()) return {{"error", {{"code", -1}, {"message", r.error().message}}}};
        return {{"result", {{"ok", true}}}};
    }
    if (method == "save_episodic_node") {
        auto node = params.at("node").get<EpisodicNode>();
        auto r = driver.save_episodic_node(node);
        if (!r.has_value()) return {{"error", {{"code", -1}, {"message", r.error().message}}}};
        return {{"result", {{"ok", true}}}};
    }
    if (method == "save_episodic_edge") {
        auto edge = params.at("edge").get<EpisodicEdge>();
        auto r = driver.save_episodic_edge(edge);
        if (!r.has_value()) return {{"error", {{"code", -1}, {"message", r.error().message}}}};
        return {{"result", {{"ok", true}}}};
    }
    if (method == "save_saga_node") {
        auto node = params.at("node").get<SagaNode>();
        auto r = driver.save_saga_node(node);
        if (!r.has_value()) return {{"error", {{"code", -1}, {"message", r.error().message}}}};
        return {{"result", {{"ok", true}}}};
    }
    if (method == "save_has_episode_edge") {
        auto r = driver.save_has_episode_edge(
            params.at("uuid").get<std::string>(),
            params.at("saga_uuid").get<std::string>(),
            params.at("episode_uuid").get<std::string>(),
            params.at("group_id").get<std::string>(),
            parse_timepoint(params.at("created_at"))
        );
        if (!r.has_value()) return {{"error", {{"code", -1}, {"message", r.error().message}}}};
        return {{"result", {{"ok", true}}}};
    }
    if (method == "save_next_episode_edge") {
        auto r = driver.save_next_episode_edge(
            params.at("uuid").get<std::string>(),
            params.at("source_episode_uuid").get<std::string>(),
            params.at("target_episode_uuid").get<std::string>(),
            params.at("group_id").get<std::string>(),
            parse_timepoint(params.at("created_at"))
        );
        if (!r.has_value()) return {{"error", {{"code", -1}, {"message", r.error().message}}}};
        return {{"result", {{"ok", true}}}};
    }

    // --- Reads (need write-lock view for consistency) ---
    if (method == "get_entity_node") {
        auto uuid = params.at("uuid").get<std::string>();
        auto r = driver.get_entity_node(uuid);
        if (!r.has_value()) {
            if (r.error().code == ErrorCode::not_found)
                return {{"result", {{"node", nullptr}}}};
            return {{"error", {{"code", -1}, {"message", r.error().message}}}};
        }
        return {{"result", {{"node", r.value()}}}};
    }
    if (method == "get_saga_by_name") {
        auto name = params.at("name").get<std::string>();
        auto group_id = params.at("group_id").get<std::string>();
        auto r = driver.get_saga_by_name(name, group_id);
        if (!r.has_value()) return {{"error", {{"code", -1}, {"message", r.error().message}}}};
        if (r.value().has_value())
            return {{"result", {{"node", r.value().value()}}}};
        return {{"result", {{"node", nullptr}}}};
    }
    if (method == "get_last_episode_in_saga") {
        auto saga_uuid = params.at("saga_uuid").get<std::string>();
        auto exclude = params.value("exclude_episode_uuid", "");
        auto r = driver.get_last_episode_in_saga(saga_uuid, exclude);
        if (!r.has_value()) return {{"error", {{"code", -1}, {"message", r.error().message}}}};
        if (r.value().has_value())
            return {{"result", {{"episode_uuid", r.value().value()}}}};
        return {{"result", {{"episode_uuid", nullptr}}}};
    }

    return {{"error", {{"code", -32601}, {"message", "Unknown method: " + method}}}};
}

// --- Main ---
int main(int argc, char** argv) {
    std::string host = "127.0.0.1";
    int port = 9876;
    std::vector<std::string> kuzu_args;  // "name=path" strings

    CLI::App app{"graphiti-kuzu-writer-server — centralized Kuzu write daemon"};
    app.add_option("--host", host, "Bind address")->default_val("127.0.0.1");
    app.add_option("--port,-p", port, "Bind port")->default_val(9876);
    app.add_option("--kuzu", kuzu_args, "Kuzu DB in name=path format (repeatable)")
        ->required()
        ->expected(-1);  // one or more
    CLI11_PARSE(app, argc, argv);

    // Parse name=path pairs
    std::vector<std::pair<std::string, std::string>> kuzu_dbs;
    for (auto& val : kuzu_args) {
        auto eq = val.find('=');
        if (eq == std::string::npos) {
            fprintf(stderr, "Error: --kuzu requires name=path format, got: %s\n", val.c_str());
            return 1;
        }
        kuzu_dbs.emplace_back(val.substr(0, eq), val.substr(eq + 1));
    }

    ix::initNetSystem();

    // Open all Kuzu DBs with write access (acquires file locks)
    std::unordered_map<std::string, std::unique_ptr<DbQueue>> queues;
    for (auto& [name, path] : kuzu_dbs) {
        fprintf(stderr, "[kuzu-writer] Opening DB '%s' at %s ...\n", name.c_str(), path.c_str());
        auto queue = std::make_unique<DbQueue>();
        queue->name = name;
        queue->driver = std::make_unique<KuzuDriver>(path, /*read_only=*/false);

        // Ensure schema and FTS indices are set up
        auto schema_result = queue->driver->setup_schema();
        if (!schema_result.has_value()) {
            fprintf(stderr, "[kuzu-writer] ERROR: Failed to setup schema for '%s': %s\n",
                    name.c_str(), schema_result.error().message.c_str());
            return 1;
        }
        auto fts_result = queue->driver->build_fts_indices();
        if (!fts_result.has_value()) {
            fprintf(stderr, "[kuzu-writer] WARNING: Failed to build FTS indices for '%s': %s\n",
                    name.c_str(), fts_result.error().message.c_str());
        }

        queue->start();
        queues[name] = std::move(queue);
        fprintf(stderr, "[kuzu-writer] ✅ DB '%s' locked and ready\n", name.c_str());
    }

    // Start WebSocket server
    ix::WebSocketServer server(port, host);

    server.setOnClientMessageCallback([&](std::shared_ptr<ix::ConnectionState> state,
                                           ix::WebSocket& ws,
                                           const ix::WebSocketMessagePtr& msg) {
        if (msg->type != ix::WebSocketMessageType::Message) return;

        auto request = json::parse(msg->str, nullptr, false);
        if (request.is_discarded()) {
            json err = {
                {"jsonrpc", "2.0"},
                {"id", nullptr},
                {"error", {{"code", -32700}, {"message", "Parse error"}}}
            };
            ws.send(err.dump());
            return;
        }

        auto id = request.value("id", json(nullptr));
        auto method = request.value("method", "");
        auto params = request.value("params", json::object());
        auto target_db = params.value("target_db", "");

        if (target_db.empty()) {
            json err = {{"jsonrpc", "2.0"}, {"id", id},
                        {"error", {{"code", -32602}, {"message", "Missing target_db in params"}}}};
            ws.send(err.dump());
            return;
        }

        auto it = queues.find(target_db);
        if (it == queues.end()) {
            json err = {{"jsonrpc", "2.0"}, {"id", id},
                        {"error", {{"code", -32602}, {"message", "Unknown target_db: " + target_db}}}};
            ws.send(err.dump());
            return;
        }

        // Capture what we need and enqueue to the DB's serial queue
        auto* queue = it->second.get();
        auto* ws_raw = &ws;

        queue->enqueue([ws_raw, id, method, params, &driver = *queue->driver] {
            json response = dispatch(driver, method, params);
            response["jsonrpc"] = "2.0";
            response["id"] = id;
            ws_raw->send(response.dump());
        });
    });

    auto res = server.listen();
    if (!res.first) {
        fprintf(stderr, "[kuzu-writer] ❌ Failed to listen on %s:%d: %s\n",
                host.c_str(), port, res.second.c_str());
        return 1;
    }

    // Register signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    server.start();
    fprintf(stderr, "[kuzu-writer] 🔧 Server listening on ws://%s:%d (%zu DBs)\n",
            host.c_str(), port, queues.size());

    // Wait for shutdown signal
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    fprintf(stderr, "\n[kuzu-writer] 🛑 Shutting down...\n");

    // Stop accepting connections
    server.stop();

    // Drain all queues
    for (auto& [name, queue] : queues) {
        queue->stop();
    }

    // Close all Kuzu DBs (releases file locks)
    queues.clear();

    fprintf(stderr, "[kuzu-writer] ✅ All locks released. Goodbye! 🏴‍☠️\n");

    ix::uninitNetSystem();
    return 0;
}
