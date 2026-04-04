#pragma once

#include <graphiti/embedder.h>

#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

namespace graphiti {

// Records every embedder call (input + output) to a JSON file.
// Wraps a real EmbedderClient and forwards all calls, tee-ing to disk.
//
// Usage (record mode):
//   auto real = std::make_unique<OpenAIEmbedder>(config.embedder);
//   auto recorder = std::make_unique<RecordingEmbedder>(std::move(real), "/tmp/recordings/test1_embedder.json");
//   Graphiti g(config, nullptr, std::move(recorder));
//
class RecordingEmbedder : public EmbedderClient {
public:
    RecordingEmbedder(std::unique_ptr<EmbedderClient> inner, const std::filesystem::path& output_path)
        : inner_(std::move(inner)), output_path_(output_path) {
        std::filesystem::create_directories(output_path.parent_path());
    }

    ~RecordingEmbedder() override { flush(); }

    std::vector<float> create(std::string_view input) override {
        auto result = inner_->create(input);

        nlohmann::json entry;
        entry["method"] = "create";
        entry["input"] = std::string(input);
        entry["output"] = result;

        std::lock_guard lock(mu_);
        recordings_.push_back(std::move(entry));

        return result;
    }

    std::vector<std::vector<float>> create_batch(const std::vector<std::string>& inputs) override {
        auto result = inner_->create_batch(inputs);

        nlohmann::json entry;
        entry["method"] = "create_batch";
        entry["inputs"] = inputs;
        entry["outputs"] = result;

        std::lock_guard lock(mu_);
        recordings_.push_back(std::move(entry));

        return result;
    }

    void flush() {
        std::lock_guard lock(mu_);
        if (recordings_.empty()) return;
        std::ofstream f(output_path_);
        if (f.is_open()) {
            f << nlohmann::json(recordings_).dump(2);
        }
    }

private:
    std::unique_ptr<EmbedderClient> inner_;
    std::filesystem::path output_path_;
    std::mutex mu_;
    nlohmann::json recordings_ = nlohmann::json::array();
};

// Replays recorded embedder responses in sequence. No network calls.
//
// Usage (replay mode):
//   auto replayer = std::make_unique<ReplayEmbedder>("/tmp/recordings/test1_embedder.json");
//   Graphiti g(config, nullptr, std::move(replayer));
//
class ReplayEmbedder : public EmbedderClient {
public:
    explicit ReplayEmbedder(const std::filesystem::path& recording_path) {
        std::ifstream f(recording_path);
        if (!f.is_open()) {
            throw std::runtime_error("Cannot open embedder recording: " + recording_path.string());
        }
        recordings_ = nlohmann::json::parse(f);
    }

    std::vector<float> create(std::string_view) override {
        if (index_ >= recordings_.size()) {
            throw std::runtime_error("ReplayEmbedder: no more recorded responses (exhausted "
                + std::to_string(recordings_.size()) + " entries)");
        }
        auto& entry = recordings_[index_++];
        if (entry["method"] != "create") {
            throw std::runtime_error("ReplayEmbedder: expected 'create' but got '"
                + entry["method"].get<std::string>() + "' at index " + std::to_string(index_ - 1));
        }
        return entry["output"].get<std::vector<float>>();
    }

    std::vector<std::vector<float>> create_batch(const std::vector<std::string>&) override {
        if (index_ >= recordings_.size()) {
            throw std::runtime_error("ReplayEmbedder: no more recorded responses (exhausted "
                + std::to_string(recordings_.size()) + " entries)");
        }
        auto& entry = recordings_[index_++];
        if (entry["method"] != "create_batch") {
            throw std::runtime_error("ReplayEmbedder: expected 'create_batch' but got '"
                + entry["method"].get<std::string>() + "' at index " + std::to_string(index_ - 1));
        }
        return entry["outputs"].get<std::vector<std::vector<float>>>();
    }

    size_t calls_replayed() const { return index_; }
    size_t total_recordings() const { return recordings_.size(); }

private:
    nlohmann::json recordings_;
    size_t index_ = 0;
};

} // namespace graphiti
