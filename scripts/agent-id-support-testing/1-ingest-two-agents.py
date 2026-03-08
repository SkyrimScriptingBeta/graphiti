"""
Step 1: Ingest episodes from two different agents into the same project.

Agent "scout" discovers information about people and places.
Agent "analyst" analyzes relationships and draws conclusions.

Both agents mention "Alice" -- so the Alice entity should end up
with agent_ids=["scout", "analyst"] after dedup merges.
"""

import asyncio
import shutil
from datetime import datetime, timezone

# ruff: noqa: E402
import sys, os  # noqa: E401

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..'))

from shared import DB_PATH, make_graphiti

EPISODES = [
    # --- Scout agent's observations ---
    {
        'name': 'scout-obs-1',
        'body': 'Alice works at Acme Corp as a software engineer. She started last month.',
        'source': 'field report',
        'agent_id': 'scout',
    },
    {
        'name': 'scout-obs-2',
        'body': 'Bob is the CTO of Acme Corp. He founded the company in 2019.',
        'source': 'field report',
        'agent_id': 'scout',
    },
    {
        'name': 'scout-obs-3',
        'body': 'Acme Corp is headquartered in Denver, Colorado.',
        'source': 'field report',
        'agent_id': 'scout',
    },
    # --- Analyst agent's conclusions ---
    {
        'name': 'analyst-note-1',
        'body': 'Alice reports to Bob at Acme Corp. She is on the infrastructure team.',
        'source': 'analysis',
        'agent_id': 'analyst',
    },
    {
        'name': 'analyst-note-2',
        'body': 'Carol is a product manager at Acme Corp. She works closely with Alice.',
        'source': 'analysis',
        'agent_id': 'analyst',
    },
]


async def main():
    # Wipe any previous test DB (Kuzu may create a file or directory)
    if os.path.exists(DB_PATH):
        print(f'Removing old test database at {DB_PATH}')
        if os.path.isdir(DB_PATH):
            shutil.rmtree(DB_PATH)
        else:
            os.remove(DB_PATH)
    # Also remove the .lock file Kuzu sometimes creates
    lock_path = DB_PATH + '.lock'
    if os.path.exists(lock_path):
        os.remove(lock_path)

    g = await make_graphiti()
    now = datetime.now(timezone.utc)

    print(f'\nIngesting {len(EPISODES)} episodes (group_id defaults to Kuzu default)...\n')

    for i, ep in enumerate(EPISODES):
        print(f'  [{i + 1}/{len(EPISODES)}] agent={ep["agent_id"]!r:12s} name={ep["name"]!r}')
        result = await g.add_episode(
            name=ep['name'],
            episode_body=ep['body'],
            source_description=ep['source'],
            reference_time=now,
            agent_id=ep['agent_id'],
        )
        print(f'           -> {len(result.nodes)} nodes, {len(result.edges)} edges extracted')

    print('\nDone! Database saved to:', DB_PATH)
    print('Run 2-search-with-agent-filter.py next.')


if __name__ == '__main__':
    asyncio.run(main())
