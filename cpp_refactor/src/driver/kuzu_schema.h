#pragma once

#include <array>
#include <string_view>

namespace graphiti::kuzu_schema {

// Extension setup (must run before FTS index creation)
constexpr std::string_view EXTENSION_QUERIES[] = {
    "INSTALL fts",
    "LOAD EXTENSION fts",
};

// Individual schema DDL statements (from Python kuzu_driver.py SCHEMA_QUERIES)
constexpr std::string_view SCHEMA_QUERIES[] = {
    R"(CREATE NODE TABLE IF NOT EXISTS Episodic (
        uuid STRING PRIMARY KEY,
        name STRING,
        group_id STRING,
        created_at TIMESTAMP,
        source STRING,
        source_description STRING,
        content STRING,
        valid_at TIMESTAMP,
        entity_edges STRING[],
        agent_id STRING DEFAULT '',
        source_id STRING DEFAULT '',
        participant_ids STRING[] DEFAULT []
    ))",

    R"(CREATE NODE TABLE IF NOT EXISTS Entity (
        uuid STRING PRIMARY KEY,
        name STRING,
        group_id STRING,
        labels STRING[],
        created_at TIMESTAMP,
        name_embedding FLOAT[],
        summary STRING,
        attributes STRING,
        agent_ids STRING[] DEFAULT [],
        source_ids STRING[] DEFAULT [],
        participant_ids STRING[] DEFAULT []
    ))",

    R"(CREATE NODE TABLE IF NOT EXISTS Community (
        uuid STRING PRIMARY KEY,
        name STRING,
        group_id STRING,
        created_at TIMESTAMP,
        name_embedding FLOAT[],
        summary STRING,
        agent_ids STRING[] DEFAULT [],
        source_ids STRING[] DEFAULT [],
        participant_ids STRING[] DEFAULT []
    ))",

    R"(CREATE NODE TABLE IF NOT EXISTS RelatesToNode_ (
        uuid STRING PRIMARY KEY,
        group_id STRING,
        created_at TIMESTAMP,
        name STRING,
        fact STRING,
        fact_embedding FLOAT[],
        episodes STRING[],
        expired_at TIMESTAMP,
        valid_at TIMESTAMP,
        invalid_at TIMESTAMP,
        attributes STRING,
        agent_ids STRING[] DEFAULT [],
        source_ids STRING[] DEFAULT [],
        participant_ids STRING[] DEFAULT []
    ))",

    R"(CREATE NODE TABLE IF NOT EXISTS Saga (
        uuid STRING PRIMARY KEY,
        name STRING,
        group_id STRING,
        created_at TIMESTAMP
    ))",

    R"(CREATE REL TABLE IF NOT EXISTS RELATES_TO(
        FROM Entity TO RelatesToNode_,
        FROM RelatesToNode_ TO Entity
    ))",

    R"(CREATE REL TABLE IF NOT EXISTS MENTIONS(
        FROM Episodic TO Entity,
        uuid STRING PRIMARY KEY,
        group_id STRING,
        created_at TIMESTAMP,
        agent_id STRING DEFAULT '',
        source_id STRING DEFAULT '',
        participant_ids STRING[] DEFAULT []
    ))",

    R"(CREATE REL TABLE IF NOT EXISTS HAS_MEMBER(
        FROM Community TO Entity,
        FROM Community TO Community,
        uuid STRING,
        group_id STRING,
        created_at TIMESTAMP
    ))",

    R"(CREATE REL TABLE IF NOT EXISTS HAS_EPISODE(
        FROM Saga TO Episodic,
        uuid STRING,
        group_id STRING,
        created_at TIMESTAMP
    ))",

    R"(CREATE REL TABLE IF NOT EXISTS NEXT_EPISODE(
        FROM Episodic TO Episodic,
        uuid STRING,
        group_id STRING,
        created_at TIMESTAMP
    ))",
};

// FTS index creation queries (must run after schema + extension setup)
constexpr std::string_view FTS_INDEX_QUERIES[] = {
    "CALL CREATE_FTS_INDEX('Episodic', 'episode_content', ['content', 'source', 'source_description'])",
    "CALL CREATE_FTS_INDEX('Entity', 'node_name_and_summary', ['name', 'summary'])",
    "CALL CREATE_FTS_INDEX('Community', 'community_name', ['name'])",
    "CALL CREATE_FTS_INDEX('RelatesToNode_', 'edge_name_and_fact', ['name', 'fact'])",
};

} // namespace graphiti::kuzu_schema
