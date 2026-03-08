# Agent ID Support Testing Scripts

Run these in order from the repo root. They share a Kuzu database on disk
so you can inspect state between steps.

## Requirements

- `OPENAI_API_KEY` environment variable set
- `uv sync --extra dev` already run

## Usage

```bash
# Step 1: Create a fresh database and ingest episodes from two agents
uv run python scripts/agent-id-support-testing/1-ingest-two-agents.py

# Step 2: Search and compare results with/without agent_ids filtering
uv run python scripts/agent-id-support-testing/2-search-with-agent-filter.py

# Step 3: Inspect raw graph data to verify agent_ids on nodes and edges
uv run python scripts/agent-id-support-testing/3-inspect-graph-data.py

# Cleanup: delete the test database
uv run python scripts/agent-id-support-testing/cleanup.py
```

Each script prints what it's doing and what it finds. You'll see the
agent attribution flowing through ingestion, dedup, and search.
