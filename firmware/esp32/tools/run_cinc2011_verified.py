"""Re-extract CinC2011 data and evaluate fixed gates and project fusions.

Run only CinC2011. Exceptions stop the run; failed records are never dropped.
Historical result tables are not inputs. Vendor algorithms remain unmodified.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import itertools
import json
import os
import platform
import sys
import time
import warnings
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

os.environ.setdefault("PYTHONDONTWRITEBYTECODE", "1")
os.environ.setdefault("OMP_NUM_THREADS", "1")
os.environ.setdefault("OPENBLAS_NUM_THREADS", "1")
os.environ.setdefault("MKL_NUM_THREADS", "1")
sys.dont_write_bytecode = True

import numpy as np
import pandas as pd
import scipy
from scipy import signal
import sklearn
from sklearn.impute import SimpleImputer
from sklearn.linear_model import LogisticRegression
from sklearn.metrics import confusion_matrix, roc_auc_score
from sklearn.model_selection import StratifiedKFold
from sklearn.neural_network import MLPClassifier
from sklearn.pipeline import make_pipeline
from sklearn.preprocessing import StandardScaler
from sklearn.svm import SVC
import wfdb

FW = Path(__file__).resolve().parents[1]
DATA = FW / "data/cinc2011_sqi"
LEAD = "lead02_II"
SQIS = ["isqi", "bsqi", "psqi", "ssqi", "ksqi", "fsqi", "bassqi"]
BASE5 = ["bsqi_f1", "spectral_entropy", "normalized_diff_rms", "template_corr", "rr_outlier_fraction"]


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def initialize(archive):
    global nk, ref, rules, ep, wqrs
    archive = Path(archive)
    vendor = archive / "vendor/neurokit2_source"
    sys.path.insert(0, str(vendor))
    import neurokit2
    from ecgdetectors import Detectors
    nk = neurokit2
    if nk.__version__ != "0.2.13" or vendor not in Path(nk.__file__).parents:
        raise RuntimeError("Unexpected NeuroKit source/version")
    folder = archive / "clifford2012_reproduction"
    ref = load("run_reproduction", folder / "run_reproduction.py")
    ref.SOURCE = DATA / "raw/extracted/set-a"
    rules = load("cinc_original_rules", folder / "compare_one_lead_categories.py")
    ep = ref.EPLimited(ref.DLL)
    wqrs = Detectors(int(ref.TARGET_FS))


def extract(record_id):
    started = time.perf_counter()
    with warnings.catch_warnings(record=True) as caught:
        warnings.simplefilter("always")
        record = wfdb.rdrecord(str(DATA / "raw/extracted/set-a" / record_id))
        if record.fs != 500 or record.sig_len != 5000 or record.sig_name[1] != "II":
            raise ValueError(f"{record_id}: unexpected format/Lead II")
        raw = np.asarray(record.p_signal[:, 1], float)
        if not np.isfinite(record.p_signal).all():
            raise ValueError(f"{record_id}: nonfinite waveform")
        # Direct library calls: no handcrafted replacement for either method.
        t = time.perf_counter()
        zhao = nk.ecg_quality(raw, sampling_rate=500, method="zhao2018", approach="simple")
        zhao_seconds = time.perf_counter() - t
        mapping = {"Unacceptable": 0., "Barely acceptable": .5, "Excellent": 1.}
        if zhao not in mapping:
            raise ValueError(f"Unexpected Zhao output {zhao!r}")
        t = time.perf_counter()
        quality = np.asarray(nk.ecg_quality(raw, sampling_rate=500, method="ho2025"))
        ho_seconds = time.perf_counter() - t
        if quality.shape != raw.shape or not np.isin(quality, [0, 1]).all():
            raise ValueError(f"{record_id}: invalid Ho output")
        # All 84 features are recalculated from all 12 raw leads.
        row = ref.extract_record(record_id, ep, wqrs)
        x = signal.resample_poly(raw, 1, 4)
        primary = ep.detect(x)
        secondary = np.asarray(wqrs.wqrs_detector(x), dtype=int)
        count = ref.ordered_matches(primary, secondary, round(.05 * 125))
        total = len(primary) + len(secondary)
        row["bsqi_f1"] = 2 * count / total if total else 0.
        row["orphanidou_accept"], row["orphanidou_score"] = rules.orphanidou_rule(x, 125, primary)
        row["kramer_accept"], row["kramer_score"] = rules.kramer_rule(raw)
        row.update(zhao_label=zhao, zhao_score=mapping[zhao], zhao_accept=int(zhao != "Unacceptable"),
                   ho_score=float(quality.mean()), ho_good_samples=int(quality.sum()),
                   zhao_seconds=zhao_seconds, ho_seconds=ho_seconds)
        centered = x - x.mean()
        rms = np.sqrt(np.mean(centered ** 2))
        row["normalized_diff_rms"] = float(np.sqrt(np.mean(np.diff(centered)**2))/rms) if rms else np.nan
        frequencies, power = signal.welch(centered, fs=125, nperseg=250)
        power = power[(frequencies >= .5) & (frequencies <= 40)]
        p = power / power.sum() if power.sum() else np.zeros_like(power)
        row["spectral_entropy"] = float(-np.sum(p*np.log(p+np.finfo(float).eps))/np.log(len(p))) if power.sum() else np.nan
        rr = np.diff(primary)*8.
        median = np.median(rr) if len(rr) else np.nan
        row["rr_outlier_fraction"] = float(np.mean((rr<300)|(rr>2000)|(rr<.5*median)|(rr>1.8*median))) if len(rr) else np.nan
        beats = []
        for peak in primary:
            left, right = peak-15, peak+25+1
            if left >= 0 and right <= len(x):
                beat = x[left:right]-np.mean(x[left:right])
                norm = np.linalg.norm(beat)
                if norm: beats.append(beat/norm)
        row["template_corr"] = np.nan
        if len(beats) >= 3:
            matrix = np.asarray(beats)
            template = np.median(matrix, axis=0)
            norm = np.linalg.norm(template)
            if norm: row["template_corr"] = float(np.mean(matrix @ (template/norm)))
        row["input_sha256"] = sha(DATA / "raw/extracted/set-a" / f"{record_id}.dat")
        row["header_sha256"] = sha(DATA / "raw/extracted/set-a" / f"{record_id}.hea")
        row["elapsed_seconds"] = time.perf_counter()-started
        row["warnings"] = json.dumps(sorted(set(str(w.message) for w in caught)))
    return row


def factory(kind):
    if kind == "Logistic": classifier = LogisticRegression(max_iter=3000, class_weight="balanced", random_state=42)
    elif kind == "RBF-SVM": classifier = SVC(C=1., gamma="scale", kernel="rbf", class_weight="balanced", random_state=42)
    elif kind == "MLP": classifier = MLPClassifier(hidden_layer_sizes=(20,), activation="tanh", solver="lbfgs", alpha=1e-4, max_iter=3000, random_state=42)
    else: raise ValueError(kind)
    return make_pipeline(SimpleImputer(strategy="median", keep_empty_features=True), StandardScaler(), classifier)


def score(model, x):
    if hasattr(model[-1], "predict_proba"): return model.predict_proba(x)[:, 1]
    return model.decision_function(x)


def stats(y, prediction, values):
    tn, fp, fn, tp = confusion_matrix(y, prediction, labels=[0, 1]).ravel()
    return dict(auc=float(roc_auc_score(y, values)), balanced_accuracy=float(.5*(tp/(tp+fn)+tn/(tn+fp))),
                sensitivity=float(tp/(tp+fn)), specificity=float(tn/(tn+fp)), coverage=float((tp+fp)/len(y)),
                bad_accepted=float(fp/(tp+fp)) if tp+fp else None,
                tp=int(tp), fp=int(fp), tn=int(tn), fn=int(fn), n=len(y))


def evaluate(frame, output):
    frame = frame[frame.quality_target.notna()].reset_index(drop=True)
    assert len(frame) == 998 and frame.record_id.is_unique
    y = frame.quality_target.astype(int).to_numpy()
    folds = list(StratifiedKFold(5, shuffle=True, random_state=42).split(frame, y))
    rows, predictions = [], []
    fold_index = np.empty(len(y), int)
    for i, (_, val) in enumerate(folds): fold_index[val] = i

    def add(name, family, columns, pred, values, lead="II", config=""):
        if not np.isfinite(values).all(): raise ValueError(f"{name}: nonfinite scores")
        row = dict(method=name, family=family, lead=lead, features="+".join(columns), configuration=config,
                   **stats(y, pred, values))
        rng = np.random.default_rng(42)
        positives, negatives = pred[y==1], 1-pred[y==0]
        bootstrap = [.5*(rng.choice(positives,len(positives)).mean()+rng.choice(negatives,len(negatives)).mean()) for _ in range(2000)]
        row["ba_ci_low"], row["ba_ci_high"] = np.quantile(bootstrap,[.025,.975])
        rows.append(row)
        predictions.append(pd.DataFrame(dict(record_id=frame.record_id, target=y, fold=fold_index,
                                              method=name, prediction=pred, score=values)))
        print(f"{name}: BA={row['balanced_accuracy']:.4f} AUC={row['auc']:.4f} coverage={row['coverage']:.4f}", flush=True)

    def learned(name, family, columns, kind, lead="II"):
        x = frame[columns].replace([np.inf,-np.inf],np.nan)
        pred, values = np.zeros(len(y),int), np.zeros(len(y))
        for train, val in folds:
            model = factory(kind).fit(x.iloc[train], y[train])
            pred[val], values[val] = model.predict(x.iloc[val]), score(model,x.iloc[val])
        add(name,family,columns,pred,values,lead,"5-fold OOF; fold-local imputation/scaling; classifier default decision threshold")

    add("No gate","baseline",[],np.ones(len(y),int),np.ones(len(y)))
    for name, prefix in [("Orphanidou","orphanidou"),("Kramer[ECGAssess]","kramer"),("Zhao[NeuroKit]","zhao")]:
        add(name,"fixed",[prefix+"_score"],frame[prefix+"_accept"].to_numpy(int),frame[prefix+"_score"].to_numpy(float),
            config="NeuroKit 0.2.13 zhao2018 approach=simple; accept Barely/Excellent" if prefix=="zhao" else "existing verified rule implementation")
    add("bSQI F1 >=0.8","fixed",["bsqi_f1"],(frame.bsqi_f1>=.8).to_numpy(int),frame.bsqi_f1.to_numpy(),config="EP Limited + WQRS; 50 ms; 2*matches/(n1+n2)")
    for threshold in [.8,.9,1.]:
        add(f"Ho[NeuroKit] >={threshold:.0%}","fixed",["ho_score"],(frame.ho_score>=threshold).to_numpy(int),frame.ho_score.to_numpy(),
            config="Direct ho2025; UNSW+NeuroKit; mean of sample-quality mask; fixed project record threshold")
    single = {"bSQI": "bsqi_f1", **{n:LEAD+"_"+n for n in SQIS if n not in ["isqi","bsqi"]}}
    for name, column in single.items():
        for kind in ["Logistic","RBF-SVM"]: learned(f"{name} {kind}","single-feature learned",[column],kind)
    sets = {"Project b+p+k+bas": [single[n] for n in ["bSQI","psqi","ksqi","bassqi"]],
            "Clifford-5 feature subset": [single[n] for n in ["bSQI","psqi","ksqi","ssqi","fsqi"]],
            "Clifford-6 single-lead subset": list(single.values())}
    for name, columns in sets.items():
        for kind in ["Logistic","RBF-SVM"]: learned(name+" "+kind,"project feature fusion",columns,kind)
    learned("Project-5SQI Logistic","project feature fusion",BASE5,"Logistic")
    fixed_scores={"bSQI":"bsqi_f1","Orphanidou":"orphanidou_score","Zhao[NeuroKit]":"zhao_score","Kramer":"kramer_score"}
    for pair in itertools.combinations(fixed_scores,2):
        for kind in ["Logistic","RBF-SVM"]:
            learned(" + ".join(pair)+" → "+kind,"project score fusion",[fixed_scores[n] for n in pair],kind)
    # Cross-fit the learned base score inside each outer training fold.
    for fusion_name in ["bSQI + Logistic-score", "All-five-score fusion"]:
        pred, values = np.zeros(len(y),int), np.zeros(len(y))
        cols = ["bsqi_f1"] if fusion_name.startswith("bSQI") else list(fixed_scores.values())
        for train,val in folds:
            xtrain=frame.iloc[train][BASE5].replace([np.inf,-np.inf],np.nan)
            xval=frame.iloc[val][BASE5].replace([np.inf,-np.inf],np.nan)
            inner=np.zeros(len(train))
            for it,iv in StratifiedKFold(3,shuffle=True,random_state=17).split(xtrain,y[train]):
                base=factory("Logistic").fit(xtrain.iloc[it],y[train][it])
                inner[iv]=score(base,xtrain.iloc[iv])
            base=factory("Logistic").fit(xtrain,y[train])
            tr=frame.iloc[train][cols].copy(); va=frame.iloc[val][cols].copy()
            tr["logistic_score"]=inner; va["logistic_score"]=score(base,xval)
            model=factory("Logistic").fit(tr,y[train])
            pred[val]=model.predict(va); values[val]=score(model,va)
        add(fusion_name,"project stacked fusion",cols+["crossfit_logistic_score"],pred,values,config="5 outer folds, 3 inner base-score folds")
    # Historical cascade thresholds locked before this CinC experiment.
    pred,values=np.zeros(len(y),int),np.zeros(len(y))
    for train,val in folds:
        model=factory("Logistic").fit(frame.iloc[train][["bsqi_f1","zhao_score"]],y[train])
        s=score(model,frame.iloc[val][["bsqi_f1","zhao_score"]]); b=frame.iloc[val].bsqi_f1.to_numpy()
        values[val]=np.where(b<=.96,0.,np.where(b>=1.,1.,s)); pred[val]=(values[val]>=.5)
    add("bSQI → Zhao[NeuroKit] cascade","project cascade",["bsqi_f1","zhao_score"],pred,values,
        config="locked bSQI cutoffs .96/1.0 from prior QTDB; inner stage Logistic default .5; 5-fold OOF")
    # CinC headers may order aVF before aVL. Use extracted header names.
    all84=[c for c in frame.columns if c.startswith("lead") and any(c.endswith("_"+s) for s in SQIS)]
    assert len(all84)==84 and all(c in frame for c in all84)
    for kind in ["Logistic","RBF-SVM","MLP"]:
        learned("Clifford-84 "+kind,"multilead reference",all84,kind,"12 leads")
    result=pd.DataFrame(rows)
    result.to_csv(output/"metrics.csv",index=False)
    pd.concat(predictions,ignore_index=True).to_csv(output/"predictions.csv",index=False)
    return result


def report(result, output):
    lines=["# CinC2011 — kết quả chạy lại", "", "Dataset: 998 record Set A có nhãn (773 acceptable, 225 unacceptable), 10 giây/record, 500 Hz. Không trộn nhiễu nhân tạo. Nhãn thuộc toàn bộ bản ghi 12 lead; input single-lead dùng II. Không chạy AF downstream.", "",
           "Gate cố định chạy trực tiếp trên waveform; learned gate dùng cùng 5-fold stratified OOF, seed 42. Không tuning trên test fold. AUC SVM dùng decision_function. Bad accepted = số record unacceptable được nhận / tổng record được nhận.", "",
           "Zhao[NeuroKit]: gọi trực tiếp NeuroKit 0.2.13, zhao2018/simple. Ho[NeuroKit]: gọi trực tiếp ho2025 với UNSW+NeuroKit; score là tỷ lệ mẫu có quality=1, không phải tỷ lệ số RR. Các ngưỡng 80/90/100% là policy ở cấp record.", "",
           "Orphanidou: rules + mean-template CC, EP Limited tại 125 Hz. Kramer: ECGAssess rules tại 500 Hz. bSQI single-lead: F1 agreement EP Limited/WQRS, 50 ms. Clifford-84: 7 SQI × 12 leads từ implementation public hiện có, EP Limited warm-up hai lượt và WQRS Python, 150 ms; tên này không có nghĩa là đã khớp số liệu private test của paper.", ""]
    for family in result.family.unique():
        lines += ["## "+family,"","| Method | BA | AUC | Coverage | Bad accepted | 95% CI BA |","|---|---:|---:|---:|---:|---|"]
        for r in result[result.family==family].itertuples():
            bad="—" if pd.isna(r.bad_accepted) else f"{100*r.bad_accepted:.2f}%"
            lines.append(f"| {r.method} | {r.balanced_accuracy:.4f} | {r.auc:.4f} | {100*r.coverage:.2f}% | {bad} | [{r.ba_ci_low:.4f}, {r.ba_ci_high:.4f}] |")
        lines.append("")
    lines += ["## Tái lập","","`features.csv`: toàn bộ feature tính lại; `predictions.csv`: từng record/method/fold; `metrics.csv`: metric và cấu hình; `manifest.json`: version, hash và protocol. Mọi fusion trong bảng là experiment của dự án. Không gọi b+p+k+bas Logistic/SVM là phương pháp Zhao gốc.",""]
    (output/"REPORT.md").write_text("\n".join(lines),encoding="utf-8")


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--archive",type=Path,default=Path(r"D:\DOC\ECG_PAPER\temp\benchmark_results"))
    parser.add_argument("--output",type=Path,default=FW/"benchmark_runs/cinc2011_20261019")
    parser.add_argument("--workers",type=int,default=4)
    args=parser.parse_args()
    args.output.mkdir(parents=True,exist_ok=True)
    if (args.output/"manifest.json").exists(): raise RuntimeError("Choose a new output directory; no overwriting completed run")
    os.environ["MPLCONFIGDIR"]=str(FW/".cache/matplotlib")
    labels=pd.read_csv(DATA/"labels.csv",dtype={"record_id":str})
    labels=labels[labels.quality_target.notna()].reset_index(drop=True)
    assert len(labels)==998 and int(labels.quality_target.sum())==773
    started=time.perf_counter()
    initialize(args.archive)
    paths=[Path(__file__), args.archive/"clifford2012_reproduction/run_reproduction.py",
           args.archive/"clifford2012_reproduction/compare_one_lead_categories.py",ref.DLL,
           Path(nk.__file__).parent/"ecg/ecg_quality.py",Path(nk.__file__).parent/"ecg/ecg_findpeaks.py",
           Path(nk.__file__).parent/"signal/signal_quality.py",DATA/"labels.csv"]
    manifest=dict(status="running",dataset="CinC2011 public Set A",records=998,acceptable=773,unacceptable=225,
                  noise="original only, no added noise",af_downstream=False,neurokit=nk.__version__,
                  python=platform.python_version(),numpy=np.__version__,scipy=scipy.__version__,sklearn=sklearn.__version__,
                  source_hashes={str(p):sha(p) for p in paths},output=str(args.output),command=sys.argv)
    (args.output/"run_status.json").write_text(json.dumps(manifest,indent=2),encoding="utf-8")
    try:
        rows=[]
        with ProcessPoolExecutor(max_workers=args.workers,initializer=initialize,initargs=(str(args.archive),)) as pool:
            for i,row in enumerate(pool.map(extract,labels.record_id,chunksize=1),1):
                rows.append(row)
                if i%25==0 or i==998:
                    print(f"Waveform extraction {i}/998, elapsed {time.perf_counter()-started:.1f}s",flush=True)
        frame=labels.merge(pd.DataFrame(rows),on="record_id",validate="one_to_one")
        frame.to_csv(args.output/"features.csv",index=False)
        metrics=evaluate(frame,args.output)
        report(metrics,args.output)
        manifest.update(status="complete",methods=len(metrics),elapsed_seconds=time.perf_counter()-started,
                        output_hashes={p.name:sha(p) for p in args.output.iterdir() if p.suffix in [".csv",".md"]})
        (args.output/"manifest.json").write_text(json.dumps(manifest,indent=2),encoding="utf-8")
    except Exception as exc:
        manifest.update(status="failed",error=repr(exc),elapsed_seconds=time.perf_counter()-started)
        raise
    finally:
        (args.output/"run_status.json").write_text(json.dumps(manifest,indent=2),encoding="utf-8")
    print(f"COMPLETE {args.output}",flush=True)


if __name__=="__main__": main()
