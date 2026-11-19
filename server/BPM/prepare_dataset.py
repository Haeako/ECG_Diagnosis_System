"""Prepare the common 57-record EDB holdout manifest; never download data."""
import argparse,sys
from pathlib import Path
sys.dont_write_bytecode=True
sys.path.insert(0,str(Path(__file__).resolve().parents[2]))
from server.modules._shared import ROOT,sha,save
from server.BPM.modules._runtime import holdout_cohort,CONDITIONS
import json

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',type=Path,default=ROOT/'datasets/prepared/BPM')
    a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
    records,excluded=holdout_cohort(ROOT)
    source=ROOT/'outputs/BPM/edb_holdout_matrix_20261111/protocol.json'
    protocol=json.loads(source.read_text());hashes={}
    folder='datasets/edb-1.0.0/european-st-t-database-1.0.0'
    for record in records:
        for suffix in ('.dat','.hea','.atr'):
            name=record+suffix;hashes[folder+'/'+name]=protocol['dataset_hashes'][name]
    for kind in ('bw','ma','em'):
        for suffix in ('.dat','.hea'):
            name='datasets/nstdb/'+kind+suffix
            hashes[name]=protocol['noise_hashes'][str(ROOT/name)]
    save(a.output/'manifest.json',dict(aspect='BPM',dataset='EDB1.0.0',records=records,excluded=excluded,conditions=CONDITIONS,
        data=folder,channel=0,source_sha256=hashes,lead_policy='header channel0 fixed before labels',
        preprocessing=protocol['afe'],noise=protocol['noise'],mixed_noise=protocol['mixed_noise'],
        leakage='33 EDB source records present in QTDB excluded for every method; reused NSTDB noise explicitly accepted'))
    print(a.output/'manifest.json')

if __name__=='__main__':main()
