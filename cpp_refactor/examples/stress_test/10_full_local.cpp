// Stress test: FULLY LOCAL Graphiti — no cloud calls whatsoever.
//
// LLM:       Gemma3 12b via Ollama (OpenAI-compatible API at localhost:11434)
// Embedder:  ONNX bge-small-en-v1.5 (384 dims)
// Graph DB:  Kuzu in-memory
//
// Set env vars to override defaults:
//   OPENAI_BASE_URL   (default: http://127.0.0.1:11434)
//   LLM_MODEL         (default: gemma3:12b)
//   OPENAI_API_KEY    (default: openllama)
//   ONNX_MODEL_DIR    (default: Skykit models path)
//   ONNX_MODEL_NAME   (default: BAAIbge-small-en-v1.5)

#include "shared.h"

#include <graphiti/embedder.h>
#include <graphiti/graphiti.h>

#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <numeric>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

// ============================================================================
// SimpleTokenizer + OnnxEmbedder (same as 9_local_onnx.cpp)
// ============================================================================

class SimpleTokenizer {
public:
    explicit SimpleTokenizer(const std::string& vocab_path) {
        std::ifstream f(vocab_path);
        std::string line;
        int32_t id = 0;
        while (std::getline(f, line)) {
            vocab_[line] = id++;
        }
    }

    std::vector<int64_t> encode(const std::string& text, int max_len = 512) const {
        std::vector<int64_t> ids;
        ids.push_back(lookup("[CLS]"));

        std::string lower = text;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        std::istringstream stream(lower);
        std::string word;
        while (stream >> word && static_cast<int>(ids.size()) < max_len - 1) {
            auto it = vocab_.find(word);
            if (it != vocab_.end()) {
                ids.push_back(it->second);
            } else {
                for (char c : word) {
                    auto cit = vocab_.find(std::string(1, c));
                    ids.push_back(cit != vocab_.end() ? cit->second : lookup("[UNK]"));
                    if (static_cast<int>(ids.size()) >= max_len - 1) break;
                }
            }
        }

        ids.push_back(lookup("[SEP]"));
        return ids;
    }

private:
    int64_t lookup(const std::string& token) const {
        auto it = vocab_.find(token);
        return (it != vocab_.end()) ? it->second : 0;
    }
    std::unordered_map<std::string, int32_t> vocab_;
};

class OnnxEmbedder : public graphiti::EmbedderClient {
public:
    OnnxEmbedder(const std::string& model_path, const std::string& vocab_path, int dims)
        : tokenizer_(vocab_path), dims_(dims)
        , env_(ORT_LOGGING_LEVEL_WARNING, "graphiti_onnx")
    {
        Ort::SessionOptions opts;
        opts.SetIntraOpNumThreads(1);
#ifdef _WIN32
        std::wstring wide_path(model_path.begin(), model_path.end());
        session_ = std::make_unique<Ort::Session>(env_, wide_path.c_str(), opts);
#else
        session_ = std::make_unique<Ort::Session>(env_, model_path.c_str(), opts);
#endif
    }

    std::vector<float> create(std::string_view input) override {
        auto ids = tokenizer_.encode(std::string(input));
        int64_t seq_len = static_cast<int64_t>(ids.size());
        std::vector<int64_t> attention_mask(seq_len, 1);
        std::vector<int64_t> token_type_ids(seq_len, 0);

        auto mem = Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeCPU);
        std::array<int64_t, 2> shape = {1, seq_len};

        auto t_ids = Ort::Value::CreateTensor<int64_t>(mem, ids.data(), ids.size(), shape.data(), shape.size());
        auto t_mask = Ort::Value::CreateTensor<int64_t>(mem, attention_mask.data(), attention_mask.size(), shape.data(), shape.size());
        auto t_type = Ort::Value::CreateTensor<int64_t>(mem, token_type_ids.data(), token_type_ids.size(), shape.data(), shape.size());

        const char* input_names[] = {"input_ids", "attention_mask", "token_type_ids"};
        const char* output_names[] = {"last_hidden_state"};

        std::vector<Ort::Value> inputs;
        inputs.push_back(std::move(t_ids));
        inputs.push_back(std::move(t_mask));
        inputs.push_back(std::move(t_type));

        auto outputs = session_->Run(Ort::RunOptions{nullptr}, input_names, inputs.data(), inputs.size(), output_names, 1);

        float* data = outputs[0].GetTensorMutableData<float>();
        std::vector<float> embedding(data, data + dims_);

        float norm = 0.0f;
        for (float v : embedding) norm += v * v;
        norm = std::sqrt(norm);
        if (norm > 0.0f) for (float& v : embedding) v /= norm;

        return embedding;
    }

    std::vector<std::vector<float>> create_batch(const std::vector<std::string>& inputs) override {
        std::vector<std::vector<float>> results;
        results.reserve(inputs.size());
        for (auto& input : inputs) results.push_back(create(input));
        return results;
    }

private:
    SimpleTokenizer tokenizer_;
    int dims_;
    Ort::Env env_;
    std::unique_ptr<Ort::Session> session_;
};

// ============================================================================
// Helpers
// ============================================================================

static std::string env_or(const char* name, const char* fallback) {
    auto* val = std::getenv(name);
    return (val && val[0]) ? std::string(val) : std::string(fallback);
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "========================================\n";
    std::cout << "  FULLY LOCAL GRAPHITI\n";
    std::cout << "  No cloud. No API tokens. Just vibes.\n";
    std::cout << "========================================\n\n";

    // --- ONNX embedder setup ---
    auto model_dir = env_or("ONNX_MODEL_DIR",
        "C:/Code/mrowr/SkyrimScriptingBeta/Skykit/read-only/v2/data/models/extracted");
    auto model_name = env_or("ONNX_MODEL_NAME", "BAAIbge-small-en-v1.5");

    int dims = 384;
    if (model_name.find("base") != std::string::npos) dims = 768;
    if (model_name.find("large") != std::string::npos) dims = 1024;

    std::string model_path = model_dir + "/" + model_name + ".onnx";
    std::string vocab_path = model_dir + "/" + model_name;
    if (model_name.find("small") != std::string::npos)
        vocab_path += "v.vocab.txt";  // typo in filename
    else
        vocab_path += ".vocab.txt";

    std::cout << "ONNX model: " << model_path << " (" << dims << " dims)\n";

    auto embedder = std::make_unique<OnnxEmbedder>(model_path, vocab_path, dims);

    // --- LLM config (Gemma3 via Ollama) ---
    graphiti::GraphitiConfig config;
    config.db_path = ":memory:";
    config.llm.base_url = env_or("OPENAI_BASE_URL", "http://127.0.0.1:11434");
    config.llm.model = env_or("LLM_MODEL", "gemma3:12b");
    config.llm.small_model = config.llm.model;  // same model for all
    config.llm.api_key = env_or("OPENAI_API_KEY", "openllama");
    config.llm.temperature = 0.7f;
    config.embedder.embedding_dim = dims;

    std::cout << "LLM: " << config.llm.model << " @ " << config.llm.base_url << "\n\n";

    // --- Create Graphiti (nullptr LLM = use OpenAI-compatible client with config above) ---
    graphiti::Graphiti g(std::move(config), nullptr, std::move(embedder));
    (void)g.build_indices();

    auto now = std::chrono::system_clock::now();

    // ========================================================================
    stress::separator("1. Ingest episode (local LLM + local ONNX)");

    auto r1 = g.add_episode(
        "ep1",
        "Alice is a software engineer who works at Acme Corp. "
        "She specializes in graph databases and uses Kuzu for her projects.",
        "test", now, graphiti::EpisodeType::message, "local_test"
    );
    stress::test("add_episode succeeded", r1.has_value());
    if (r1.has_value()) {
        stress::test("extracted nodes", !r1.value().nodes.empty());
        stress::test("extracted edges", !r1.value().edges.empty());
        std::cout << "  Nodes: ";
        for (auto& n : r1.value().nodes) std::cout << "[" << n.name << "] ";
        std::cout << "\n  Edges: ";
        for (auto& e : r1.value().edges) std::cout << "[" << e.fact << "] ";
        std::cout << "\n";
    }

    // ========================================================================
    stress::separator("2. Ingest second episode (dedup test)");

    auto r2 = g.add_episode(
        "ep2",
        "Bob is Alice's colleague at Acme Corp. "
        "Bob is working on a new search feature using embeddings.",
        "test", now + std::chrono::seconds(60),
        graphiti::EpisodeType::message, "local_test"
    );
    stress::test("second episode succeeded", r2.has_value());
    if (r2.has_value()) {
        std::cout << "  Nodes: ";
        for (auto& n : r2.value().nodes) std::cout << "[" << n.name << "] ";
        std::cout << "\n  Edges: ";
        for (auto& e : r2.value().edges) std::cout << "[" << e.fact << "] ";
        std::cout << "\n";
    }

    // ========================================================================
    stress::separator("3. Search (local ONNX cosine + local LLM rerank)");

    auto search = g.search("graph database engineer", "local_test");
    stress::test("search succeeded", search.has_value());
    if (search.has_value()) {
        stress::test("search returned results", !search.value().empty());
        for (auto& e : search.value()) {
            std::cout << "  -> " << e.fact << "\n";
        }
    }

    // ========================================================================
    stress::separator("4. Search for Bob");

    auto search2 = g.search("Bob embeddings", "local_test");
    stress::test("bob search succeeded", search2.has_value());
    if (search2.has_value()) {
        for (auto& e : search2.value()) {
            std::cout << "  -> " << e.fact << "\n";
        }
    }

    // ========================================================================
    stress::separator("5. Token tracking (LLM usage)");

    auto usage = g.token_tracker().get_total_usage();
    stress::test("input tokens tracked", usage.input_tokens > 0);
    stress::test("output tokens tracked", usage.output_tokens > 0);
    std::cout << "  Total tokens: " << usage.total_tokens() << "\n";
    std::cout << "  Total LLM calls: " << g.token_tracker().get_total_calls() << "\n";

    auto breakdown = g.token_tracker().get_usage();
    for (auto& [name, entry] : breakdown) {
        std::cout << "    " << name << ": " << entry.call_count << " calls, "
                  << entry.total_tokens() << " tokens\n";
    }

    // ========================================================================
    stress::summary();
    return stress::failed > 0 ? 1 : 0;
}
