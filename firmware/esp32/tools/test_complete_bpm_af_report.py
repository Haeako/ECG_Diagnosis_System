import unittest
import numpy as np
from complete_bpm_af_report import match, pure_label, af_features, classification, error_metrics

class ProtocolTest(unittest.TestCase):
    def test_one_to_one_matching(self):
        pairs=match(np.array([0,1000,2000]),np.array([5,6,2005]))
        self.assertEqual(pairs.tolist(),[[0,0],[2,2]])
        # Gap must not become a single adjacent RR.
        self.assertFalse(np.all(np.diff(pairs,axis=0)==1))

    def test_hidden_rhythm_transition(self):
        changes=(np.array([0.,450.,550.]),np.array(['N','AFIB','N']))
        self.assertEqual(pure_label(0,1000,changes),-1)
        self.assertEqual(pure_label(600,1000,changes),0)
        self.assertEqual(pure_label(460,540,changes),1)

    def test_long_timestamps(self):
        peaks=np.arange(31,dtype=np.uint32)*1000+20_000_000
        rows,count=af_features(peaks,(np.array([0]),np.array(['AFIB'])),'x')
        self.assertEqual(count['valid'],1)
        self.assertEqual(rows[0]['mean_rr_ms'],1000)
        fs=250
        self.assertNotEqual((peaks[:1]*fs/1000).item(),(peaks[:1].astype(float)*fs/1000).item())

    def test_missing_prediction_denominator(self):
        m=error_metrics([0,5,np.nan,6])
        self.assertEqual(m['coverage'],.75)
        self.assertEqual(m['within_5_all'],.5)

    def test_confusion(self):
        m=classification([0,0,1,1],[0,1,0,1],[.1,.7,.2,.8])
        self.assertEqual(m['balanced_accuracy'],.5)
        self.assertEqual(m['windows'],4)

if __name__=='__main__':unittest.main()
