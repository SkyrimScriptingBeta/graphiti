#include <catch2/catch_all.hpp>
#include <nlohmann/json.hpp>

#include <graphiti/config.h>

#include "llm/openai_client.h"
#include "llm/response_models.h"

#include <cstdlib>
#include <string>

using namespace graphiti;
using json = nlohmann::json;

static LLMConfig make_config() {
    auto* key = std::getenv("OPENAI_API_KEY");
    if (!key || std::string(key).empty()) {
        SKIP("OPENAI_API_KEY not set");
    }
    auto full = GraphitiConfig::from_env();
    return full.llm;
}

TEST_CASE("OpenAI LLM: simple JSON response", "[integration][llm]") {
    auto config = make_config();
    OpenAIClient client(config);

    std::vector<Message> messages = {
        {"system", "You are a helpful assistant that responds in JSON."},
        {"user", "Return a JSON object with a single key 'greeting' and value 'hello'."},
    };

    auto result = client.generate_response(messages);
    if (!result.has_value()) {
        FAIL("LLM error: " << result.error().message);
    }
    CHECK(result.value().contains("greeting"));
    CHECK(result.value()["greeting"] == "hello");
}

TEST_CASE("OpenAI LLM: structured output with schema", "[integration][llm]") {
    auto config = make_config();
    OpenAIClient client(config);

    std::vector<Message> messages = {
        {"system", "You extract entities from text."},
        {"user", "Extract entities from: 'Alice works at Acme Corp in Denver.'"},
    };

    auto result = client.generate_response(
        messages,
        response_schemas::EXTRACTED_ENTITIES,
        ModelSize::small
    );
    REQUIRE(result.has_value());

    auto entities = result.value().get<ExtractedEntities>();
    CHECK(!entities.extracted_entities.empty());

    // Should find at least Alice
    bool found_alice = false;
    for (auto& e : entities.extracted_entities) {
        if (e.name.find("Alice") != std::string::npos) {
            found_alice = true;
        }
    }
    CHECK(found_alice);
}

TEST_CASE("OpenAI LLM: small model", "[integration][llm]") {
    auto config = make_config();
    OpenAIClient client(config);

    std::vector<Message> messages = {
        {"system", "You respond in JSON."},
        {"user", "Return {\"number\": 42}"},
    };

    auto result = client.generate_response(messages, std::nullopt, ModelSize::small);
    REQUIRE(result.has_value());
    CHECK(result.value()["number"] == 42);
}

TEST_CASE("OpenAI LLM: bad API key returns error", "[integration][llm]") {
    LLMConfig config;
    config.api_key = "sk-bad-key-for-testing";
    OpenAIClient client(config);

    std::vector<Message> messages = {
        {"system", "You respond in JSON."},
        {"user", "Return {\"x\": 1}"},
    };

    auto result = client.generate_response(messages);
    REQUIRE(!result.has_value());
    CHECK(result.error().code == ErrorCode::llm_error);
}
