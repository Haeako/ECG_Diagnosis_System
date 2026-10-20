import unittest
import numpy as np
import pandas as pd
from sklearn.model_selection import StratifiedGroupKFold
from run_butqdb_survey import OUT,read_frame
from evaluate_butqdb_survey import metrics,cluster_ci,method_specs,fit_predict
from report_butqdb_survey import paired_cluster_delta,main_report_mask

class SurveyTests(unittest.TestCase):
    def test_required_methods(self):
        specs=method_specs();self.assertEqual(len(specs),42)
        self.assertEqual(len({s['method'] for s in specs}),42)
        names=' '.join(s['method'] for s in specs)
        for method in ['Orphanidou','Ho[NeuroKit]','Zhao[NeuroKit]','Kramer','Clifford-5','b+p+k+bas']:
            self.assertIn(method,names)
        self.assertNotIn('84',names)
        self.assertNotIn('Zhao-6',names)

    def test_report_scope_partition(self):
        frame=pd.DataFrame([dict(task=task,**spec) for task in ['qrs_target','morphology_target'] for spec in method_specs()])
        mask=main_report_mask(frame)
        self.assertEqual(int(mask.sum()),28)
        self.assertEqual(int((~mask).sum()),56)
        self.assertEqual(set(frame[mask].task),{'qrs_target'})
        self.assertFalse(frame[mask].family.isin(['project score fusion','project stacked fusion']).any())
        self.assertEqual(int((frame[mask].family=='project feature fusion').sum()),7)
        self.assertEqual(int((frame[mask].family=='project cascade').sum()),1)
        self.assertEqual(len(frame),len(frame[mask])+len(frame[~mask]))

    def test_bad_accepted_denominator(self):
        m=metrics(np.array([0,0,0,1,1]),np.array([1,0,0,1,1]),np.array([.7,.2,.1,.9,.8]))
        self.assertAlmostEqual(m['bad_accepted'],1/3)
        self.assertAlmostEqual(m['coverage'],3/5)
        self.assertAlmostEqual(m['bad_pass_rate'],1/3)

    def test_abstain_all(self):
        m=metrics(np.array([0,1]),np.array([0,0]),np.array([0.,0.]))
        self.assertTrue(np.isnan(m['bad_accepted']))
        self.assertEqual(m['coverage'],0)
        self.assertEqual(m['balanced_accuracy'],.5)

    def test_subject_partition(self):
        w=read_frame(OUT/'windows.csv')
        self.assertEqual(w.subject_id.nunique(),15)
        self.assertEqual(w.window_id.nunique(),35773)
        self.assertTrue((w.end_sample-w.start_sample==10000).all())
        self.assertTrue((w.start_sample%10000==0).all())
        self.assertTrue((w.groupby('subject_id').fold.nunique()==1).all())
        self.assertTrue(np.array_equal(w.qrs_target.to_numpy(),(w.class3_fraction==0).to_numpy(int)))
        self.assertTrue(np.array_equal(w.morphology_target.to_numpy(),(w.class1_fraction==1).to_numpy(int)))
        for fold in w.fold.unique():
            train=w[w.fold!=fold];groups=train.subject_id.to_numpy()
            for it,iv in StratifiedGroupKFold(3,shuffle=True,random_state=17).split(train,train.qrs_target,groups):
                self.assertFalse(set(groups[it])&set(groups[iv]))
                self.assertEqual(train.iloc[it].qrs_target.nunique(),2)
                self.assertEqual(train.iloc[it].morphology_target.nunique(),2)

    def test_cluster_bootstrap(self):
        m=cluster_ci(np.array([0,1,0,1]),np.array([0,1,0,1]),np.array(['a','a','b','b']))
        self.assertEqual(m['ba_ci_low'],1)
        self.assertEqual(m['ba_ci_high'],1)

    def test_paired_bootstrap_identical(self):
        c=np.array([[8,2,1,9],[4,1,3,7]])
        m=paired_cluster_delta(c,c)
        self.assertEqual(m['delta_ba'],0)
        self.assertEqual(m['ci_low'],0)
        self.assertEqual(m['ci_high'],0)

    def test_paired_bootstrap_perfect_vs_inverted(self):
        m=paired_cluster_delta(np.array([[5,0,0,5],[10,0,0,10]]),np.array([[0,5,5,0],[0,10,10,0]]))
        self.assertEqual(m['delta_ba'],1)
        self.assertEqual(m['ci_low'],1)
        self.assertEqual(m['ci_high'],1)

    def test_replay_predictions(self):
        """Rerun fixed gates and representative learned/stack/cascade paths."""
        frame=read_frame(OUT/'windows.csv').merge(read_frame(OUT/'features.csv'),on='window_id',validate='one_to_one')
        for task in ['qrs_target','morphology_target']:
            y=frame[task].to_numpy(int)
            for index in [*range(8),26,39,40,41]:
                with self.subTest(task=task,index=index):
                    pred,values,_=fit_predict(method_specs()[index],frame,y)
                    with np.load(OUT/task/f'{index:02d}_predictions.npz',allow_pickle=False) as saved:
                        np.testing.assert_array_equal(pred,saved['prediction'])
                        np.testing.assert_allclose(values,saved['score'],rtol=1e-10,atol=1e-10)

if __name__=='__main__':unittest.main()
