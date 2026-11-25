"""Prepare frozen BUTQDB labels, subject folds and waveform checksums."""
import argparse,json,sys,shutil
from pathlib import Path
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from SQI.utils.reproducibility import SEED, lock_random_seed
if __name__ == '__main__':
    lock_random_seed(restart_for_hash=True)
import numpy as np
import pandas as pd
from SQI.utils._shared import ROOT,sha,save

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,default=ROOT/'datasets/prepared/SQI');a=p.parse_args()
    source=ROOT/'report/benchmark_runs/butqdb_20261020'
    labels=pd.read_csv(source/'windows.csv',dtype={'record_id':str,'subject_id':str})
    assert len(labels)==labels.window_id.nunique()==35773 and labels.record_id.nunique()==18 and labels.subject_id.nunique()==15
    assert labels.groupby('subject_id').fold.nunique().max()==1
    assert np.array_equal(labels.qrs_target,labels.class3_fraction.eq(0).astype(int))
    original=json.loads((source/'protocol.json').read_text());expected={Path(p).name:h for p,h in original['source_hashes'].items()}
    a.output.mkdir(parents=True,exist_ok=True);shutil.copyfile(source/'windows.csv',a.output/'windows.csv')
    hashes={f'datasets/butqdb_workspace/data/{r}_ECG_ecg.npy':expected[f'{r}_ECG_ecg.npy'] for r in labels.record_id.unique()}
    hashes[str((a.output/'windows.csv').resolve().relative_to(ROOT))]=sha(a.output/'windows.csv')
    save(a.output/'manifest.json',dict(aspect='SQI',dataset='BUTQDB',data='datasets/butqdb_workspace/data',labels=str((a.output/'windows.csv').resolve().relative_to(ROOT)),
        windows=35773,subjects=15,records=18,source_sha256=hashes,target='class1+2 good; any class3 bad; unknown excluded',
        provider='Shared native C primary-peak cache at outputs/SQI/continuous_profile_20261110/replay; no annotation peaks',
        no_training_or_threshold_selection=True))
    print(a.output/'manifest.json')

if __name__=='__main__':main()
