#include <graphiti/type_definitions.h>

#include <algorithm>
#include <format>
#include <fstream>
#include <sstream>

#include <yaml-cpp/yaml.h>

namespace graphiti {

Result<TypeDefinitions> TypeDefinitions::from_yaml_string(std::string_view yaml) {
    TypeDefinitions defs;

    YAML::Node root;
    try {
        root = YAML::Load(std::string(yaml));
    } catch (const YAML::Exception& e) {
        return std::unexpected(GraphitiError{
            ErrorCode::invalid_config,
            std::format("Failed to parse YAML: {}", e.what())
        });
    }

    // Parse entity_types
    if (root["entity_types"] && root["entity_types"].IsMap()) {
        for (auto it = root["entity_types"].begin(); it != root["entity_types"].end(); ++it) {
            EntityTypeDef def;
            def.name = it->first.as<std::string>();

            auto val = it->second;
            if (val["description"]) {
                def.description = val["description"].as<std::string>();
            }
            if (val["fields"] && val["fields"].IsMap()) {
                for (auto fit = val["fields"].begin(); fit != val["fields"].end(); ++fit) {
                    def.fields[fit->first.as<std::string>()] = fit->second.as<std::string>();
                }
            }
            defs.entity_types.push_back(std::move(def));
        }
    }

    // Parse edge_types
    if (root["edge_types"] && root["edge_types"].IsMap()) {
        for (auto it = root["edge_types"].begin(); it != root["edge_types"].end(); ++it) {
            EdgeTypeDef def;
            def.name = it->first.as<std::string>();

            auto val = it->second;
            if (val["description"]) {
                def.description = val["description"].as<std::string>();
            }
            if (val["source"]) {
                def.source_type = val["source"].as<std::string>();
            }
            if (val["target"]) {
                def.target_type = val["target"].as<std::string>();
            }
            defs.edge_types.push_back(std::move(def));
        }
    }

    // Parse excluded_entity_types
    if (root["exclude_entity_types"] && root["exclude_entity_types"].IsSequence()) {
        for (const auto& item : root["exclude_entity_types"]) {
            defs.excluded_entity_types.push_back(item.as<std::string>());
        }
    }

    return defs;
}

Result<TypeDefinitions> TypeDefinitions::from_yaml_file(std::string_view path) {
    auto path_str = std::string(path);
    std::ifstream file(path_str);
    if (!file.is_open()) {
        return std::unexpected(GraphitiError{
            ErrorCode::invalid_config,
            std::format("Cannot open YAML file: {}", path)
        });
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    return from_yaml_string(ss.str());
}

std::string TypeDefinitions::entity_types_prompt_json() const {
    nlohmann::json arr = nlohmann::json::array();

    // Entity is always type 0 unless excluded
    if (!is_excluded("Entity")) {
        arr.push_back({
            {"entity_type_id", 0},
            {"entity_type_name", "Entity"},
            {"entity_type_description", "A general entity"}
        });
    }

    // Custom types start at id 1
    int id = 1;
    for (auto& def : entity_types) {
        if (!is_excluded(def.name)) {
            arr.push_back({
                {"entity_type_id", id},
                {"entity_type_name", def.name},
                {"entity_type_description", def.description}
            });
        }
        ++id;
    }

    return arr.dump();
}

nlohmann::json TypeDefinitions::edge_types_prompt_json() const {
    nlohmann::json arr = nlohmann::json::array();

    for (auto& def : edge_types) {
        nlohmann::json entry;
        entry["fact_type_name"] = def.name;
        entry["fact_type_description"] = def.description;
        entry["fact_type_signatures"] = nlohmann::json::array();
        entry["fact_type_signatures"].push_back(
            nlohmann::json::array({def.source_type, def.target_type})
        );
        arr.push_back(std::move(entry));
    }

    return arr;
}

std::string TypeDefinitions::attribute_schema_for(std::string_view type_name) const {
    for (auto& def : entity_types) {
        if (def.name == type_name) {
            nlohmann::json schema;
            schema["type"] = "object";
            schema["properties"] = nlohmann::json::object();

            for (auto& [field_name, field_desc] : def.fields) {
                schema["properties"][field_name] = {
                    {"type", nlohmann::json::array({"string", "null"})},
                    {"description", field_desc}
                };
            }
            return schema.dump();
        }
    }
    return "{}";
}

std::string TypeDefinitions::resolve_type_name(int entity_type_id) const {
    if (entity_type_id == 0) return "Entity";
    int idx = entity_type_id - 1;
    if (idx >= 0 && idx < static_cast<int>(entity_types.size())) {
        return entity_types[idx].name;
    }
    return "Entity";
}

bool TypeDefinitions::is_excluded(std::string_view type_name) const {
    return std::find(excluded_entity_types.begin(), excluded_entity_types.end(), type_name)
        != excluded_entity_types.end();
}

} // namespace graphiti
