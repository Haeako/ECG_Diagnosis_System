"""Regression checks for metric semantics and independent reproduction reports."""
import json
from pathlib import Path
import sys
import unittest
import numpy as np
import pandas as pd

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from BPM import metrics as bpm
from SQI import metrics as sqi
from AFib import metrics as af
from BPM.utils._summary import verify_reference
from BPM.utils._shared import checked


class MetricProtocolTests(unittest.TestCase):
    def test_tolerance_is_inclusive_and_matching_is_one_to_one(self):
        pairs = bpm.match(np.array([100., 200.]), np.array([50., 51., 250.]), 50)
        np.testing.assert_array_equal(pairs, [[0, 0], [1, 2]])

    def test_bpm_error_is_not_filtered_by_qrs_tolerance(self):
        rows = bpm.score(dict(reference_ms=np.arange(0, 6000, 1000.),
                              prediction_ms=np.array([100., 1600., 3100.]),
                              bpm_end_ms=np.array([5000.]), bpm_values=np.array([120.])))
        self.assertNotEqual(rows[0]['TP'], rows[1]['TP'])
        for row in rows:
            self.assertEqual(row['bpm_mae'], 60.)
            self.assertEqual(row['bpm_rmse'], 60.)

    def test_absent_bpm_keeps_reference_denominator(self):
        counts, _, _, errors = bpm.summarize_arrays(
            np.arange(0, 11000, 1000.), np.array([]),
            np.array([5000., 10000.]), np.array([60., np.nan]), 50)
        result = bpm.errors(errors, 'bpm')
        self.assertEqual(counts['bpm_reference_windows'], 2)
        self.assertEqual(result['bpm_total'], 2)
        self.assertEqual(result['bpm_coverage'], .5)

    def test_sqi_uses_same_valid_cohort_for_all_methods(self):
        labels = pd.DataFrame({'window_id':range(4),'qrs_target':[0,1,0,1]})
        scores = pd.DataFrame({'window_id':[0,1,2,3]*2,'method':['a']*4+['b']*4,
                               'score':[0.,1.,0.,1.,0.,1.,np.nan,1.]})
        result = sqi.summarize(scores, labels).set_index('method')
        self.assertTrue(result.common_valid.eq(3).all())
        self.assertEqual(result.loc['a','coverage'], 1.)
        self.assertEqual(result.loc['b','coverage'], .75)

    def test_duplicate_sqi_scores_are_rejected(self):
        with self.assertRaisesRegex(ValueError, 'Duplicate'):
            sqi.summarize(pd.DataFrame({'window_id':[1,1],'method':['a','a'],'score':[0,1]}),
                          pd.DataFrame({'window_id':[1],'qrs_target':[1]}))

    def test_af_population_and_confusion(self):
        result = af.score([0,1,1], [.1,.9,.8], [0,1,0], population=4)
        self.assertEqual((result['TP'],result['TN'],result['FN']), (1,1,1))
        self.assertEqual(result['coverage'], .75)
        self.assertEqual(result['sensitivity'], .5)


class ReproductionIntegrityTests(unittest.TestCase):
    def test_independent_baselines_and_missing_methods(self):
        for domain, name, backend in [('BPM','host','host'),('BPM','device','c'),
                                       ('SQI','SQI',None),('AFib','AFib',None)]:
            with self.subTest(domain=domain, backend=backend):
                frame = pd.read_csv(ROOT/'references'/domain/(name+'.csv'))
                verify_reference(domain, backend, frame)
                with self.assertRaisesRegex(ValueError, 'Incomplete'):
                    verify_reference(domain, backend, frame.iloc[:-1])

    def test_changed_numeric_result_is_not_published(self):
        frame = pd.read_csv(ROOT/'references/BPM/host.csv')
        frame.loc[0,'mae'] += .01
        with self.assertRaises(AssertionError):
            verify_reference('BPM','host',frame)

    def test_wrong_input_hash_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'Changed input'):
            checked(ROOT/'references/BPM/host.csv', '0'*64)

    def test_bpm_selected_ports_match_final_device_methods(self):
        selected = json.loads((ROOT/'BPM/ESP/selected_methods.json').read_text())
        expected = pd.read_csv(ROOT/'references/BPM/device.csv')
        self.assertEqual(set(selected['methods']),set(expected.method))
        self.assertEqual(len(selected['methods']),12)


if __name__ == '__main__':
    unittest.main()
