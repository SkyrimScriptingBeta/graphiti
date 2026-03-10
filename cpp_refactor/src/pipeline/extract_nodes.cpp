#include "extract_nodes.h"

#include "llm/response_models.h"
#include "prompts/prompts.h"
#include "utils/uuid.h"

#include <format>

namespace graphiti::pipeline {

Result<std::vector<EntityNode>> extract_nodes(
    LLMClient& llm,
    const ExtractNodesInput& input
) {
    // Build prompt based on episode type
    std::vector<Message> messages;
    if (input.episode_type == EpisodeType::message) {
        messages = prompts::extract_message(
            input.entity_types, input.previous_episodes,
            input.episode_content, input.custom_instructions
        );
    } else if (input.episode_type == EpisodeType::json) {
        messages = prompts::extract_json(
            input.entity_types, input.source_description,
            input.episode_content, input.custom_instructions
        );
    } else {
        messages = prompts::extract_text(
            input.entity_types, input.episode_content, input.custom_instructions
        );
    }

    // Call LLM
    auto llm_result = llm.generate_response(
        messages, response_schemas::EXTRACTED_ENTITIES, ModelSize::small
    );
    if (!llm_result.has_value()) {
        return std::unexpected(llm_result.error());
    }

    // Parse response
    ExtractedEntities extracted;
    try {
        extracted = llm_result.value().get<ExtractedEntities>();
    } catch (const std::exception& e) {
        return std::unexpected(GraphitiError{
            ErrorCode::llm_parse_error,
            std::format("Failed to parse extracted entities: {}", e.what())
        });
    }

    // Convert to EntityNode objects
    auto now = std::chrono::system_clock::now();
    std::vector<EntityNode> nodes;
    nodes.reserve(extracted.extracted_entities.size());

    for (auto& entity : extracted.extracted_entities) {
        // Resolve type name from entity_type_id when type_defs is available
        std::string type_name;
        if (input.type_defs) {
            type_name = input.type_defs->resolve_type_name(entity.entity_type_id);

            // Skip nodes whose resolved type is excluded
            if (input.type_defs->is_excluded(type_name)) {
                continue;
            }
        }

        EntityNode node;
        node.uuid = uuid::generate();
        node.name = std::move(entity.name);
        node.group_id = input.group_id;
        node.created_at = now;

        // Set labels from resolved type
        if (input.type_defs && !type_name.empty() && type_name != "Entity") {
            node.labels = {"Entity", type_name};
        }

        nodes.push_back(std::move(node));
    }

    return nodes;
}

// Attribute extraction prompt schema (JSON object with nullable string fields)
static constexpr std::string_view ENTITY_ATTRIBUTES_SCHEMA = R"({
  "type": "object",
  "properties": {
    "attributes": {
      "type": "object",
      "description": "Extracted attribute values for the entity"
    }
  },
  "required": ["attributes"],
  "title": "EntityAttributes"
})";

VoidResult extract_entity_attributes(
    LLMClient& llm,
    std::vector<EntityNode>& nodes,
    const TypeDefinitions& type_defs,
    std::string_view episode_content
) {
    for (auto& node : nodes) {
        // Find the custom type label (the non-"Entity" label)
        std::string custom_type;
        for (auto& label : node.labels) {
            if (label != "Entity") {
                custom_type = label;
                break;
            }
        }

        if (custom_type.empty()) continue;

        // Check if this type has fields defined
        std::string schema = type_defs.attribute_schema_for(custom_type);
        if (schema == "{}") continue;

        // Check that the type actually has fields
        bool has_fields = false;
        for (auto& def : type_defs.entity_types) {
            if (def.name == custom_type && !def.fields.empty()) {
                has_fields = true;
                break;
            }
        }
        if (!has_fields) continue;

        // Build prompt for attribute extraction
        std::string sys = "You are an AI assistant that extracts entity attributes from text. "
                          "Extract the requested attribute values for the given entity based on "
                          "the provided context. Return null for any attributes that cannot be "
                          "determined from the context.";

        std::string user = std::format(
            R"(Given the following text, extract attributes for the entity "{0}" (type: {1}).

<TEXT>
{2}
</TEXT>

<ATTRIBUTE_SCHEMA>
{3}
</ATTRIBUTE_SCHEMA>

Extract the attribute values based on what is stated or clearly implied in the text.
Return a JSON object with an "attributes" key containing the extracted values.
Use null for any attribute that cannot be determined from the context.)",
            node.name, custom_type, episode_content, schema
        );

        auto result = llm.generate_response(
            {{"system", std::move(sys)}, {"user", std::move(user)}},
            ENTITY_ATTRIBUTES_SCHEMA, ModelSize::small
        );

        if (result.has_value()) {
            try {
                auto& response = result.value();
                if (response.contains("attributes") && response["attributes"].is_object()) {
                    node.attributes = response["attributes"];
                }
            } catch (...) {
                // Attribute extraction failure is non-fatal
            }
        }
    }

    return {};
}

} // namespace graphiti::pipeline
