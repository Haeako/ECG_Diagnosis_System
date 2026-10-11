"""One-to-one scoring contract for the production-C replay benchmark."""
import runpy
from pathlib import Path

score = runpy.run_path(str(Path(__file__).parents[1] / 'experiments/firmware_qtdb_benchmark.py'))['scores']

def test_duplicate_is_false_positive():
    result = score([1000], [990, 1010])
    assert (result['TP'], result['FP'], result['FN']) == (1, 1, 0)

def test_outside_tolerance_is_unmatched_on_both_sides():
    result = score([1000], [1051])
    assert (result['TP'], result['FP'], result['FN']) == (0, 1, 1)

def test_tolerance_includes_boundary():
    assert score([1000, 2000], [950, 2050])['TP'] == 2

def test_ordered_matching_does_not_steal_later_match():
    # Nearest-first can consume 1040 for reference 1000, losing the pair
    # for 1080. Chronological matching correctly keeps both valid pairs.
    assert score([1000, 1080], [950, 1040])['TP'] == 2

def test_empty_reference_counts_all_detections():
    result = score([], [1000, 2000])
    assert (result['TP'], result['FP'], result['FN']) == (0, 2, 0)
