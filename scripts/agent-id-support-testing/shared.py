"""Shared setup for agent-id testing scripts."""

import os
import sys

# Make sure OPENAI_API_KEY is set before we import anything that tries to use it
if not os.environ.get('OPENAI_API_KEY'):
    print('ERROR: Set OPENAI_API_KEY environment variable first')
    sys.exit(1)

DB_PATH = os.path.join(os.path.dirname(__file__), '_test_kuzu_db')


async def make_graphiti():
    """Create a Graphiti instance backed by a Kuzu database on disk."""
    from graphiti_core.driver.kuzu_driver import KuzuDriver
    from graphiti_core.graphiti import Graphiti

    driver = KuzuDriver(db=DB_PATH)
    g = Graphiti(graph_driver=driver)
    await g.build_indices_and_constraints()
    return g
