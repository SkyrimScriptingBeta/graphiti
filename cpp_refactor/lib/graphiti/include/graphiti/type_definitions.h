#pragma once

#include <graphiti/error.h>

#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace graphiti {

struct EntityTypeDef {
    std::string name;
    std::string description;
    std::map<std::string, std::string> fields; // field_name -> description
};

struct EdgeTypeDef {
    std::string name;
    std::string description;
    std::string source_type; // entity type name (e.g. "Person")
    std::string target_type; // entity type name (e.g. "Organization")
};

struct TypeDefinitions {
    std::vector<EntityTypeDef> entity_types;
    std::vector<EdgeTypeDef> edge_types;
    std::vector<std::string> excluded_entity_types;

    // Load from YAML file or string
    static Result<TypeDefinitions> from_yaml_file(std::string_view path);
    static Result<TypeDefinitions> from_yaml_string(std::string_view yaml);

    // Build the entity_types JSON string for prompts (includes Entity as type 0 unless excluded)
    std::string entity_types_prompt_json() const;

    // Build the edge_types JSON for prompts (fact_type format)
    nlohmann::json edge_types_prompt_json() const;

    // Build a JSON schema string for attribute extraction of a given entity type
    std::string attribute_schema_for(std::string_view type_name) const;

    // Look up an entity type name by its type_id (0 = Entity, 1+ = custom types in order)
    std::string resolve_type_name(int entity_type_id) const;

    // Check if a type name is excluded
    bool is_excluded(std::string_view type_name) const;
};

} // namespace graphiti
