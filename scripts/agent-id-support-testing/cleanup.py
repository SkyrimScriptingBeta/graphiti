"""Remove the test Kuzu database."""

import glob
import os
import shutil

# ruff: noqa: E402
import sys  # noqa: E401

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..'))

from shared import DB_PATH

cleaned = False

# Kuzu may create a file, a directory, or both plus a .lock file
for path in glob.glob(DB_PATH + '*'):
    if os.path.isdir(path):
        shutil.rmtree(path)
        print(f'Removed directory {path}')
    elif os.path.isfile(path):
        os.remove(path)
        print(f'Removed file {path}')
    cleaned = True

if not cleaned:
    print('Nothing to clean up.')
