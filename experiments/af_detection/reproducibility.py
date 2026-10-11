"""Shared deterministic settings for the AF experiments."""

from __future__ import annotations

import os
import random

import numpy as np


RANDOM_SEED = 42


def lock_random_seed() -> int:
    """Seed Python and NumPy; estimators must also receive this return value."""
    os.environ["PYTHONHASHSEED"] = str(RANDOM_SEED)
    random.seed(RANDOM_SEED)
    np.random.seed(RANDOM_SEED)
    return RANDOM_SEED
