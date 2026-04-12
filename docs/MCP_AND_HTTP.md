# Graphiti Python — MCP & HTTP API Reference

> Quick-reference notes. The MCP server and REST server expose the same surface area — MCP wraps it for AI tool-use over stdio, REST is a standard FastAPI service run via uvicorn.

---

## 🔌 MCP Server Tools (9 tools)

Source: `mcp_server/src/graphiti_mcp_server.py`

| Tool | What it does |
|---|---|
| `add_memory` | Ingest content — takes a name, messages, and group context, builds the knowledge graph |
| `search_nodes` | Search entity nodes by query with optional entity type filters |
| `search_memory_facts` | Search edges/facts (relationships between entities) by query |
| `get_entity_edge` | Fetch a specific edge by UUID |
| `get_episodes` | List episodes, filterable by group IDs and count |
| `delete_entity_edge` | Delete a specific edge by UUID |
| `delete_episode` | Delete a specific episode by UUID |
| `clear_graph` | Delete all data for given group IDs |
| `get_status` | Health check — server + database connection status |

---

## 🌐 REST Server Endpoints

### Retrieval — `server/graph_service/routers/retrieve.py`

| Method | Path | What it does |
|---|---|---|
| `POST` | `/search` | Hybrid search across the graph (semantic + keyword + traversal) |
| `GET` | `/entity-edge/{uuid}` | Get a specific edge by UUID |
| `GET` | `/episodes/{group_id}` | Get last N episodes for a group |
| `POST` | `/get-memory` | Retrieve memory context (combines search + episodes) |

### Ingestion — `server/graph_service/routers/ingest.py`

| Method | Path | What it does |
|---|---|---|
| `POST` | `/messages` | Add messages to be processed into the graph |
| `POST` | `/entity-node` | Manually add an entity node |
| `DELETE` | `/entity-edge/{uuid}` | Delete a specific edge |
| `DELETE` | `/group/{group_id}` | Delete an entire group |
| `DELETE` | `/episode/{uuid}` | Delete a specific episode |
| `POST` | `/clear` | Clear graph data |

### Root — `server/graph_service/main.py`

| Method | Path | What it does |
|---|---|---|
| `GET` | `/healthcheck` | Basic health check |

---

## Core Operations

The whole API boils down to: **ingest content → build graph → search it → manage it**.

No CLI exists in the Python repo — it's purely library + REST server + MCP server.
