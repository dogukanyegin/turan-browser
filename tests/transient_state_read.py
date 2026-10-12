"""Read an asynchronously replaced fixture file without an exists/open race.

The application deliberately quarantines invalid metadata before atomically
writing its recovery. Only the expected missing-path gap is retriable. Never
mask permission failures, invalid restored contents or permanently missing data.
"""
from pathlib import Path


def read_optional_bytes(path: Path):
    """Return bytes or None for a *missing* path, propagate all other errors."""
    try:
        return path.read_bytes()
    except FileNotFoundError:
        return None
