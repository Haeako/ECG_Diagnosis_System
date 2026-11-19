"""Prepare AFDB evaluation cohort and all prepared SHDB fusion sessions."""
import argparse,json,sys
from pathlib import Path
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parents[2]))
from server.modules._shared import ROOT,sha,save
import pandas as pd

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,default=ROOT/'datasets/prepared/AFib');a=p.parse_args()
    afdb=ROOT/'outputs/AFib/afdb_frozen_full_20261118'
    source=json.loads((afdb/'protocol.json').read_text());base=json.loads((afdb/'base_models.json').read_text())
    records=['04936','05091','05261','06453','07859','08434']
    assert not set(records)&set(base['classifiers']['train_records'])
    features='outputs/AFib/afdb_frozen_full_20261118/predictions.csv'
    audit=json.loads((afdb/'publication_audit.json').read_text());assert audit['status']=='PASS' and sha(ROOT/features)==audit['predictions_sha256']
    shdb='experiment/report/benchmark_runs/af_fusion_published_swt_20261103'
    plan=json.loads((ROOT/shdb/'inventory.json').read_text())
    train=pd.read_csv(ROOT/shdb/'paired_predictions.csv',usecols=['group_id','session_id','dataset'])
    assert train.group_id.nunique()==93 and train.session_id.nunique()==294 and train.dataset.eq('SHDB-AF').all()
    hashes={k:v for k,v in source['file_sha256'].items() if k.startswith('datasets/afdb/')}
    for name in (features,shdb+'/inventory.json',shdb+'/paired_predictions.csv'):hashes[name]=sha(ROOT/name)
    a.output.mkdir(parents=True,exist_ok=True)
    save(a.output/'manifest.json',dict(aspect='AFib',records=records,data='datasets/afdb',channel=0,source_sha256=hashes,
        afdb_feature_cache=features,shdb_inventory=shdb+'/inventory.json',shdb_feature_cache=shdb+'/paired_predictions.csv',
        shdb_subjects=93,shdb_sessions=294,shdb_scope='All prepared SHDB sessions, not all raw-record seconds',
        evaluation='18470 exact common pure-label AFDB10s windows; no fit or threshold tuning on AFDB',
        pretraining='Official ECGFounder checkpoint; complete pretraining record manifest unavailable, user accepted',
        base_train_records=base['classifiers']['train_records']))
    print(a.output/'manifest.json')

if __name__=='__main__':main()
