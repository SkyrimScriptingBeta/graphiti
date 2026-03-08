"""
Step 3: Inspect raw graph data to verify agent attribution.

This script queries the Kuzu database directly to show:
  - Episodes and their agent_id
  - Entity nodes and their agent_ids (should accumulate from dedup)
  - Entity edges (via RelatesToNode_) and their agent_ids
  - Episodic edges (MENTIONS) and their agent_id
"""

import asyncio

# ruff: noqa: E402
import sys, os  # noqa: E401

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..'))

from shared import make_graphiti


async def main():
    g = await make_graphiti()
    driver = g.driver

    # --- Episodes ---
    print('\n' + '=' * 70)
    print('  EPISODES (each has a single agent_id)')
    print('=' * 70)
    records, _, _ = await driver.execute_query(
        'MATCH (e:Episodic) RETURN e.name AS name, e.agent_id AS agent_id, e.uuid AS uuid'
    )
    for r in records:
        print(f'  {r["name"]:25s} agent_id={r["agent_id"]!r}')

    # --- Entity Nodes ---
    print('\n' + '=' * 70)
    print('  ENTITY NODES (agent_ids accumulates through dedup)')
    print('=' * 70)
    records, _, _ = await driver.execute_query(
        'MATCH (n:Entity) RETURN n.name AS name, n.agent_ids AS agent_ids, n.uuid AS uuid'
    )
    for r in records:
        agent_ids = r.get('agent_ids') or []
        marker = ' <-- BOTH AGENTS' if len(agent_ids) > 1 else ''
        print(f'  {r["name"]:25s} agent_ids={agent_ids}{marker}')

    # --- Entity Edges (RelatesToNode_) ---
    print('\n' + '=' * 70)
    print('  ENTITY EDGES (agent_ids on RelatesToNode_)')
    print('=' * 70)
    records, _, _ = await driver.execute_query(
        """
        MATCH (src:Entity)-[:RELATES_TO]->(e:RelatesToNode_)-[:RELATES_TO]->(tgt:Entity)
        RETURN src.name AS src, e.name AS name, e.fact AS fact,
               e.agent_ids AS agent_ids, tgt.name AS tgt
        """
    )
    for r in records:
        agent_ids = r.get('agent_ids') or []
        marker = ' <-- BOTH' if len(agent_ids) > 1 else ''
        print(f'  {r["src"]} --[{r["name"]}]--> {r["tgt"]}')
        print(f'    fact: {r["fact"]}')
        print(f'    agent_ids: {agent_ids}{marker}')

    # --- Episodic Edges (MENTIONS) ---
    print('\n' + '=' * 70)
    print('  EPISODIC EDGES (MENTIONS - each has single agent_id)')
    print('=' * 70)
    records, _, _ = await driver.execute_query(
        """
        MATCH (ep:Episodic)-[m:MENTIONS]->(n:Entity)
        RETURN ep.name AS episode, n.name AS entity, m.agent_id AS agent_id
        """
    )
    for r in records:
        print(f'  {r["episode"]:25s} --MENTIONS--> {r["entity"]:20s} agent_id={r["agent_id"]!r}')

    # --- Summary stats ---
    print('\n' + '=' * 70)
    print('  SUMMARY')
    print('=' * 70)

    records, _, _ = await driver.execute_query(
        'MATCH (n:Entity) WHERE size(n.agent_ids) > 1 RETURN count(*) AS multi_agent_count'
    )
    multi = records[0]['multi_agent_count'] if records else 0

    records, _, _ = await driver.execute_query('MATCH (n:Entity) RETURN count(*) AS total')
    total = records[0]['total'] if records else 0

    print(f'  Total entity nodes:              {total}')
    print(f'  Nodes with multiple agent_ids:    {multi}')
    if multi > 0:
        print(f'\n  Dedup merge is working -- {multi} entities have contributions from both agents.')
    else:
        print('\n  No multi-agent nodes. The LLM may have extracted different entity names')
        print('  for each agent, so no dedup merging occurred. This is still valid!')

    print('\nDone! Run cleanup.py when finished.')


if __name__ == '__main__':
    asyncio.run(main())
