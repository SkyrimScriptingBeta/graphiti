// Stress test: fully local Graphiti with ONNX embeddings (no OpenAI for embeddings).
//
// Uses onnxruntime to load a BAAI/bge model and produce embeddings locally.
// Still uses OpenAI for LLM (swap in CallbackLLMClient + local Gemma3 when ready).
//
// Set ONNX_MODEL_PATH to the .onnx file and ONNX_VOCAB_PATH to the vocab.
// Defaults to bge-small-en-v1.5 (384 dims) for fast testing.

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
// Simple WordPiece-ish tokenizer for BGE models
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

    // Tokenize into input_ids with [CLS] ... [SEP]
    std::vector<int64_t> encode(const std::string& text, int max_len = 512) const {
        std::vector<int64_t> ids;
        ids.push_back(lookup("[CLS]"));

        // Simple whitespace + lowercase tokenization
        std::string lower = text;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        std::istringstream stream(lower);
        std::string word;
        while (stream >> word && static_cast<int>(ids.size()) < max_len - 1) {
            auto it = vocab_.find(word);
            if (it != vocab_.end()) {
                ids.push_back(it->second);
            } else {
                // Character-level fallback
                for (char c : word) {
                    auto cit = vocab_.find(std::string(1, c));
                    if (cit != vocab_.end()) {
                        ids.push_back(cit->second);
                    } else {
                        ids.push_back(lookup("[UNK]"));
                    }
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

// ============================================================================
// ONNX Embedder using onnxruntime
// ============================================================================

class OnnxEmbedder : public graphiti::EmbedderClient {
public:
    OnnxEmbedder(const std::string& model_path, const std::string& vocab_path, int dims)
        : tokenizer_(vocab_path)
        , dims_(dims)
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

        // Build attention_mask (all 1s) and token_type_ids (all 0s)
        std::vector<int64_t> attention_mask(seq_len, 1);
        std::vector<int64_t> token_type_ids(seq_len, 0);

        // Create tensors
        auto memory_info = Ort::MemoryInfo::CreateCpu(OrtDeviceAllocator, OrtMemTypeCPU);
        std::array<int64_t, 2> shape = {1, seq_len};

        auto input_ids_tensor = Ort::Value::CreateTensor<int64_t>(
            memory_info, ids.data(), ids.size(), shape.data(), shape.size());
        auto attention_mask_tensor = Ort::Value::CreateTensor<int64_t>(
            memory_info, attention_mask.data(), attention_mask.size(), shape.data(), shape.size());
        auto token_type_ids_tensor = Ort::Value::CreateTensor<int64_t>(
            memory_info, token_type_ids.data(), token_type_ids.size(), shape.data(), shape.size());

        // Run inference
        const char* input_names[] = {"input_ids", "attention_mask", "token_type_ids"};
        const char* output_names[] = {"last_hidden_state"};

        std::vector<Ort::Value> inputs;
        inputs.push_back(std::move(input_ids_tensor));
        inputs.push_back(std::move(attention_mask_tensor));
        inputs.push_back(std::move(token_type_ids_tensor));

        auto outputs = session_->Run(
            Ort::RunOptions{nullptr},
            input_names, inputs.data(), inputs.size(),
            output_names, 1
        );

        // Extract [CLS] token embedding (first token, all dims)
        float* data = outputs[0].GetTensorMutableData<float>();
        std::vector<float> embedding(data, data + dims_);

        // L2 normalize
        float norm = 0.0f;
        for (float v : embedding) norm += v * v;
        norm = std::sqrt(norm);
        if (norm > 0.0f) {
            for (float& v : embedding) v /= norm;
        }

        return embedding;
    }

    std::vector<std::vector<float>> create_batch(const std::vector<std::string>& inputs) override {
        std::vector<std::vector<float>> results;
        results.reserve(inputs.size());
        for (auto& input : inputs) {
            results.push_back(create(input));
        }
        return results;
    }

private:
    SimpleTokenizer tokenizer_;
    int dims_;
    Ort::Env env_;
    std::unique_ptr<Ort::Session> session_;
};

// ============================================================================
// Main
// ============================================================================

int main() {
    // Resolve model paths
    const char* model_dir_env = std::getenv("ONNX_MODEL_DIR");
    std::string model_dir = model_dir_env
        ? model_dir_env
        : "C:/Code/mrowr/SkyrimScriptingBeta/Skykit/read-only/v2/data/models/extracted";

    // Default to bge-small (384 dims, fastest)
    const char* model_name_env = std::getenv("ONNX_MODEL_NAME");
    std::string model_name = model_name_env ? model_name_env : "BAAIbge-small-en-v1.5";

    int dims = 384;
    if (model_name.find("base") != std::string::npos) dims = 768;
    if (model_name.find("large") != std::string::npos) dims = 1024;

    std::string model_path = model_dir + "/" + model_name + ".onnx";
    // Note: small model has a typo in vocab filename ("v.vocab" vs ".vocab")
    std::string vocab_path = model_dir + "/" + model_name;
    if (model_name.find("small") != std::string::npos) {
        vocab_path += "v.vocab.txt";
    } else {
        vocab_path += ".vocab.txt";
    }

    std::cout << "Model: " << model_path << "\n";
    std::cout << "Vocab: " << vocab_path << "\n";
    std::cout << "Dims:  " << dims << "\n\n";

    // ========================================================================
    stress::separator("1. ONNX Embedder standalone test");

    auto embedder = std::make_unique<OnnxEmbedder>(model_path, vocab_path, dims);

    auto vec = embedder->create("Hello world");
    stress::test("embedding has correct dimensions", static_cast<int>(vec.size()) == dims);
    stress::test("embedding is normalized (L2 ~ 1.0)",
                 std::abs(std::sqrt(std::inner_product(
                     vec.begin(), vec.end(), vec.begin(), 0.0f)) - 1.0f) < 0.01f);

    auto vec2 = embedder->create("Hello world");
    stress::test("same input produces same embedding", vec == vec2);

    auto vec3 = embedder->create("Completely different topic about quantum physics");
    float dot = std::inner_product(vec.begin(), vec.end(), vec3.begin(), 0.0f);
    stress::test("different inputs produce different embeddings (cosine < 0.95)", dot < 0.95f);

    // ========================================================================
    stress::separator("2. Graphiti with local ONNX embedder + OpenAI LLM");

    // Still need OpenAI for LLM (swap for CallbackLLMClient + Gemma3 later)
    auto api_key = stress::require_api_key();

    graphiti::GraphitiConfig config;
    config.db_path = ":memory:";
    config.llm.api_key = api_key;
    config.embedder.embedding_dim = dims;  // match ONNX model dims

    // Create a second embedder instance for Graphiti (it takes ownership)
    auto graphiti_embedder = std::make_unique<OnnxEmbedder>(model_path, vocab_path, dims);

    // nullptr for LLM = use default OpenAI from config
    // Custom ONNX embedder replaces OpenAI embeddings
    graphiti::Graphiti g(
        std::move(config),
        nullptr,  // LLM: use OpenAI from config (swap for Gemma3 CallbackLLMClient later)
        std::move(graphiti_embedder)
    );
    (void)g.build_indices();

    auto now = std::chrono::system_clock::now();
    auto r = g.add_episode(
        "ep1",
        "Alice is a software engineer at Acme Corp. She works on the Bridge project.",
        "test", now, graphiti::EpisodeType::message, "local_test"
    );
    stress::test("add_episode succeeded with ONNX embeddings", r.has_value());
    if (r.has_value()) {
        stress::test("extracted nodes", !r.value().nodes.empty());
        stress::test("extracted edges", !r.value().edges.empty());
    }

    // ========================================================================
    stress::separator("3. Search with ONNX embeddings");

    auto search = g.search("software engineer", "local_test");
    stress::test("search succeeded", search.has_value());
    if (search.has_value()) {
        stress::test("search returned results", !search.value().empty());
        if (!search.value().empty()) {
            std::cout << "  Top result: " << search.value()[0].fact << "\n";
        }
    }

    // ========================================================================
    stress::separator("4. Token tracking still works with custom embedder");
    auto usage = g.token_tracker().get_total_usage();
    stress::test("LLM tokens tracked (input > 0)", usage.input_tokens > 0);
    stress::test("LLM tokens tracked (output > 0)", usage.output_tokens > 0);
    std::cout << "  Total LLM tokens: " << usage.total_tokens() << "\n";

    stress::summary();
    return stress::failed > 0 ? 1 : 0;
}
