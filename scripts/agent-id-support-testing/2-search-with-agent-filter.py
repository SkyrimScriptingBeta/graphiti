"""
Step 2: Search with and without agent_ids filtering.

This script runs the same query three ways:
  1. No agent filter  -> should return results from both agents
  2. agent_ids=["scout"]   -> only scout's contributions
  3. agent_ids=["analyst"] -> only analyst's contributions

You should see different result sets for each.
"""

import asyncio

# ruff: noqa: E402
import sys, os  # noqa: E401

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..'))

from shared import make_graphiti

QUERY = 'Alice'


def print_edges(edges, label):
    print(f'\n{"=" * 60}')
    print(f'  {label}')
    print(f'  Query: {QUERY!r}')
    print(f'  Results: {len(edges)}')
    print(f'{"=" * 60}')
    for i, edge in enumerate(edges):
        agent_info = f'  agent_ids={edge.agent_ids}'
        print(f'  [{i + 1}] {edge.fact}{agent_info}')
    if not edges:
        print('  (no results)')


async def main():
    g = await make_graphiti()

    # Kuzu uses empty-string group_id by default; pass it explicitly for search
    group_ids = ['']

    # 1. No agent filter
    edges_all = await g.search(QUERY, group_ids=group_ids)
    print_edges(edges_all, 'ALL AGENTS (no filter)')

    # 2. Scout only
    edges_scout = await g.search(QUERY, group_ids=group_ids, agent_ids=['scout'])
    print_edges(edges_scout, 'SCOUT ONLY (agent_ids=["scout"])')

    # 3. Analyst only
    edges_analyst = await g.search(QUERY, group_ids=group_ids, agent_ids=['analyst'])
    print_edges(edges_analyst, 'ANALYST ONLY (agent_ids=["analyst"])')

    # Summary
    print(f'\n{"=" * 60}')
    print('  SUMMARY')
    print(f'{"=" * 60}')
    print(f'  All agents:   {len(edges_all)} results')
    print(f'  Scout only:   {len(edges_scout)} results')
    print(f'  Analyst only: {len(edges_analyst)} results')

    if len(edges_all) > len(edges_scout) or len(edges_all) > len(edges_analyst):
        print('\n  Agent filtering is working -- filtered results are a subset.')
    elif len(edges_all) == 0:
        print('\n  No results at all. The LLM may not have extracted edges for this query.')
    else:
        print('\n  All counts are the same -- entities may have both agents in agent_ids')
        print('  (which is correct if both agents mentioned the same facts).')

    print('\nRun 3-inspect-graph-data.py to see the raw agent attribution on nodes and edges.')


if __name__ == '__main__':
    asyncio.run(main())
