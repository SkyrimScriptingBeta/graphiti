# Kuzu Writer Daemon

Centralized write daemon that holds exclusive `.kuzu` file locks so multiple extraction processes can run in parallel without lock contention.

## Why

Kuzu uses file-level locking. Two processes writing to the same `.kuzu` = instant hard failure. The daemon owns all the write locks. Extraction processes send writes over WebSocket. Problem solved.

## Build

```bash
xmake build graphiti-kuzu-writer-server
```

## Run

```bash
graphiti-kuzu-writer-server \
  --port 9876 \
  --kuzu keel=$LOCALAPPDATA/CollabLite/agents/keel/knowledge \
  --kuzu wrench=$LOCALAPPDATA/CollabLite/agents/wrench/knowledge \
  --kuzu pixel=$LOCALAPPDATA/CollabLite/agents/pixel/knowledge \
  --kuzu tiller=$LOCALAPPDATA/CollabLite/agents/tiller/knowledge \
  --kuzu purr=$LOCALAPPDATA/CollabLite/agents/purr/knowledge
```

### CLI Options

| Flag | Default | Description |
|------|---------|-------------|
| `--host` | `127.0.0.1` | Bind address |
| `--port` / `-p` | `9876` | Bind port |
| `--kuzu NAME=PATH` | (required) | Kuzu DB, repeatable. `NAME` is the target ID clients use. `PATH` is the `.kuzu` directory. |

### What happens on startup

1. Opens every `.kuzu` DB with a write connection (acquires file locks)
2. Runs `setup_schema()` on each
3. Starts a per-DB serial write queue (queues drain in parallel across DBs)
4. Starts WebSocket server

### Shutdown

Ctrl+C (SIGINT) or SIGTERM:
1. Stops accepting connections
2. Drains all queues (finishes in-flight writes)
3. Closes all Kuzu connections (releases file locks)

## Client Configuration

Set two environment variables on the extraction process:

```bash
export KUZU_WRITER_URI=ws://127.0.0.1:9876
export KUZU_WRITER_TARGET_DB=keel
```

Or set them in `GraphitiConfig`:

```cpp
config.kuzu_writer_uri = "ws://127.0.0.1:9876";
config.kuzu_writer_target_db = "keel";
```

When `kuzu_writer_uri` is set:
- All `save_*()` calls go over WebSocket to the daemon
- Reads that need write-locked consistency (`get_entity_node` for merge checks, saga lookups) also go through the daemon
- Other reads (search, retrieve_episodes, dedup) stay local
- If the daemon connection fails, Graphiti falls back to direct Kuzu writes with a warning

When `kuzu_writer_uri` is empty (default): direct Kuzu writes, zero behavior change.

## Full Example

```bash
# Terminal 1: Start the daemon
graphiti-kuzu-writer-server --port 9876 \
  --kuzu keel=$LOCALAPPDATA/CollabLite/agents/keel/knowledge \
  --kuzu wrench=$LOCALAPPDATA/CollabLite/agents/wrench/knowledge

# Terminal 2: Extraction process targeting keel's graph
KUZU_WRITER_URI=ws://127.0.0.1:9876 KUZU_WRITER_TARGET_DB=keel \
  collablite ingest-session <session-1> --agent keel

# Terminal 3: Another extraction process, ALSO targeting keel — no lock conflict!
KUZU_WRITER_URI=ws://127.0.0.1:9876 KUZU_WRITER_TARGET_DB=keel \
  collablite ingest-session <session-2> --agent keel

# Terminal 4: Different agent, same daemon
KUZU_WRITER_URI=ws://127.0.0.1:9876 KUZU_WRITER_TARGET_DB=wrench \
  collablite ingest-session <session-3> --agent wrench
```

## Protocol

JSON-RPC 2.0 over WebSocket. Every request includes `target_db` in params to route to the correct DB queue.

### Write request
```json
{
  "jsonrpc": "2.0",
  "id": "1",
  "method": "save_entity_node",
  "params": {
    "target_db": "keel",
    "node": { /* EntityNode JSON */ }
  }
}
```

### Write response
```json
{"jsonrpc": "2.0", "id": "1", "result": {"ok": true}}
```

### Supported methods

**Writes:** `save_entity_node`, `save_entity_node_embedding`, `save_entity_edge`, `save_entity_edge_embedding`, `save_episodic_node`, `save_episodic_edge`, `save_saga_node`, `save_has_episode_edge`, `save_next_episode_edge`

**Reads:** `get_entity_node`, `get_saga_by_name`, `get_last_episode_in_saga`

## Files

| File | What |
|------|------|
| `src/server/kuzu_writer_server.cpp` | The daemon binary |
| `src/remote/kuzu_writer_client.h` | WebSocket client header |
| `src/remote/kuzu_writer_client.cpp` | WebSocket client implementation |
| `include/graphiti/config.h` | `kuzu_writer_uri` + `kuzu_writer_target_db` config fields |
| `src/graphiti.cpp` | Branch logic: `if (has_writer()) writer_client->save_*() else driver.save_*()` |
