"""BPM error and one-to-one QRS tolerance metrics. No inference or fitting."""
import numpy as np
import pandas as pd
from types import SimpleNamespace
def match(ref, est, tol=50):
    pairs=[]; i=j=0
    while i<len(ref) and j<len(est):
        if est[j]<ref[i]-tol: j+=1
        elif est[j]>ref[i]+tol: i+=1
        else:
            pairs.append((i,j)); i+=1; j+=1
    return np.asarray(pairs,dtype=int).reshape(-1,2)

host=SimpleNamespace(match=match)

def bpm_from_peaks(peaks, ends):
    rr = np.diff(peaks)
    return np.array([60000/rr[(peaks[1:]>end-5000)&(peaks[1:]<=end)].mean()
                     if np.any((peaks[1:]>end-5000)&(peaks[1:]<=end)) else np.nan for end in ends])

def summarize_arrays(ref, pred, bpm_ends, bpm_values, tolerance):
    pairs = host.match(ref, pred, tolerance)
    tp = len(pairs)
    timing = pred[pairs[:, 1]] - ref[pairs[:, 0]]
    adjacent = np.all(np.diff(pairs, axis=0) == 1, axis=1)
    left, right = pairs[:-1][adjacent], pairs[1:][adjacent]
    rr = (pred[right[:, 1]]-pred[left[:, 1]]) - (ref[right[:, 0]]-ref[left[:, 0]])
    ref_bpm = bpm_from_peaks(ref, bpm_ends)
    eligible = np.isfinite(ref_bpm)
    bpm_error = np.where(bpm_values > 0, bpm_values, np.nan) - ref_bpm
    # Preserve denominator for eligible reference windows with absent predictions.
    bpm_error = bpm_error[eligible]
    return dict(TP=tp, FP=len(pred)-tp, FN=len(ref)-tp, reference_rr=max(0,len(ref)-1),
                bpm_reference_windows=int(eligible.sum())), timing, rr, bpm_error

def errors(x, prefix):
    x = np.asarray(x, float)
    valid = x[np.isfinite(x)]
    return {f'{prefix}_n': len(valid), f'{prefix}_total': len(x),
            f'{prefix}_coverage': len(valid)/len(x) if len(x) else None,
            f'{prefix}_mae': float(np.abs(valid).mean()) if len(valid) else None,
            f'{prefix}_rmse': float(np.sqrt(np.mean(valid**2))) if len(valid) else None,
            f'{prefix}_p95_abs': float(np.percentile(np.abs(valid),95)) if len(valid) else None}

def compute_metrics(counts, timing, rr, bpm):
    tp, fp, fn = counts['TP'], counts['FP'], counts['FN']
    return dict(**counts, sensitivity=tp/(tp+fn) if tp+fn else None,
                ppv=tp/(tp+fp) if tp+fp else None, f1=2*tp/(2*tp+fp+fn) if 2*tp+fp+fn else None,
                **errors(timing,'timing_ms'), **errors(rr,'rr_ms'), **errors(bpm,'bpm'),
                rr_reference_coverage=len(rr)/counts['reference_rr'] if counts['reference_rr'] else None,
                bpm_within_5_all=float(np.sum(np.abs(bpm)<=5)/len(bpm)) if len(bpm) else None)

def score(arrays):
    result=[]
    for tolerance in (50,150):
        counts,timing,rr,bpm=summarize_arrays(arrays['reference_ms'],arrays['prediction_ms'],arrays['bpm_end_ms'],arrays['bpm_values'],tolerance)
        if 'reference_bpm' in arrays:
            expected=arrays['reference_bpm'];valid=np.isfinite(expected)
            bpm=np.where(arrays['bpm_values']>0,arrays['bpm_values'],np.nan)[valid]-expected[valid]
            counts['bpm_reference_windows']=int(valid.sum())
        result.append(dict(tolerance_ms=tolerance,**compute_metrics(counts,timing,rr,bpm)))
    return result

def reference_bpm_context(ref,ends):
    values=np.full(len(ends),np.nan)
    for i,end in enumerate(ends):
        context=ref[(ref>=end-10000)&(ref<end)]
        rr=np.diff(context);rr=rr[context[1:]>=end-5000]
        if len(rr):values[i]=60000/rr.mean()
    return values

def aggregate(frame):
    cells=[]
    for (method,condition,tolerance),g in frame.groupby(['method','condition','tolerance_ms']):
        tp,fp,fn=[g[k].sum() for k in ('TP','FP','FN')]
        n=g.bpm_n.sum();total=g.bpm_total.sum()
        cells.append(dict(method=method,condition=condition,tolerance_ms=tolerance,records=len(g),
            f1=2*tp/(2*tp+fp+fn) if 2*tp+fp+fn else np.nan,
            bpm_mae=(g.bpm_mae.fillna(0)*g.bpm_n).sum()/n if n else np.nan,
            bpm_rmse=np.sqrt((g.bpm_rmse.fillna(0)**2*g.bpm_n).sum()/n) if n else np.nan,
            bpm_coverage=n/total if total else 0))
    return pd.DataFrame(cells)

def summarize(cells):
    rows=[]
    for method,g in cells.groupby('method'):
        a=g[g.tolerance_ms.eq(50)];b=g[g.tolerance_ms.eq(150)]
        if set(a.condition)!=set(b.condition):raise ValueError('Different tolerance cohorts')
        rows.append(dict(method=method,conditions=len(a),records_per_condition=int(a.records.min()),
            mae=a.bpm_mae.mean(),rmse=a.bpm_rmse.mean(),f1_50=a.f1.mean(),f1_150=b.f1.mean(),coverage=a.bpm_coverage.min()))
    return pd.DataFrame(rows).sort_values('rmse')
