"""Build auditable report from complete subject-disjoint BUTQDB experiments."""
import json
import platform
import sys
from pathlib import Path
import numpy as np
import pandas as pd
import scipy
import sklearn
from sklearn.metrics import confusion_matrix,roc_auc_score
from run_butqdb_survey import OUT,DATA,ARCHIVE,FW,read_frame,save_json,sha
from evaluate_butqdb_survey import TASKS,method_specs

def paired_cluster_delta(counts,reference,draws=2000):
    """Paired BA difference: same resampled subjects for method and reference."""
    counts=np.asarray(counts,dtype=np.int64);reference=np.asarray(reference,dtype=np.int64)
    assert counts.shape==reference.shape and counts.ndim==2 and counts.shape[1]==4
    assert np.array_equal(counts[:,:2].sum(axis=1),reference[:,:2].sum(axis=1))
    assert np.array_equal(counts[:,2:].sum(axis=1),reference[:,2:].sum(axis=1))
    indices=np.random.default_rng(42).integers(len(counts),size=(draws,len(counts)))
    a=counts[indices].sum(axis=1);b=reference[indices].sum(axis=1)
    good=(a[:,2]+a[:,3]>0)&(a[:,0]+a[:,1]>0)
    def ba(c):return .5*(c[...,3]/(c[...,2]+c[...,3])+c[...,0]/(c[...,0]+c[...,1]))
    delta=ba(a[good])-ba(b[good])
    return dict(delta_ba=float(ba(counts.sum(axis=0))-ba(reference.sum(axis=0))),
                ci_low=float(np.quantile(delta,.025)),ci_high=float(np.quantile(delta,.975)),
                bootstrap_valid=int(good.sum()))

def table(frame,columns):
    def text(v):
        if pd.isna(v):return '—'
        if isinstance(v,(float,np.floating)):return f'{v:.4f}'
        return str(v).replace('|','/')
    return '\n'.join(['| '+' | '.join(columns)+' |','|'+'|'.join(['---']*len(columns))+'|']+
                     ['| '+' | '.join(text(v) for v in row)+' |' for row in frame[columns].itertuples(index=False,name=None)])

def main():
    specs=method_specs();windows=read_frame(OUT/'windows.csv');features=read_frame(OUT/'features.csv')
    protocol=json.loads((OUT/'protocol.json').read_text(encoding='utf-8'))
    assert len(features)==len(windows)==protocol['windows']
    assert features.window_id.is_unique and set(features.window_id)==set(windows.window_id)
    frame=windows.merge(features,on='window_id',validate='one_to_one')
    rows=[];subjects=[];strata=[];costs=[];hashes={};warnings=[];subject_counts={}
    for task in TASKS:
        for i,spec in enumerate(specs):
            folder=OUT/task;stem=f'{i:02d}'
            path=folder/f'{stem}_metrics.json';pfile=folder/f'{stem}_predictions.npz'
            assert path.exists() and pfile.exists(),f'Incomplete: {task}/{spec["method"]}'
            result=json.loads(path.read_text(encoding='utf-8'))
            assert result['task']==task and result['method']==spec['method'] and result['columns']==spec['columns']
            saved_subjects=json.loads((folder/f'{stem}_subjects.json').read_text())
            with np.load(pfile,allow_pickle=False) as p:
                assert np.array_equal(p['window_id'],frame.window_id.to_numpy(str))
                assert np.array_equal(p['target'],frame[task].to_numpy(int))
                assert np.array_equal(p['fold'],frame.fold.to_numpy())
                assert np.array_equal(p['subject'],frame.subject_id.to_numpy(str))
                y,pred,score=p['target'],p['prediction'],p['score']
                tn,fp,fn,tp=map(int,confusion_matrix(y,pred,labels=[0,1]).ravel())
                assert [tn,fp,fn,tp]==[result[k] for k in ['tn','fp','fn','tp']]
                assert abs(.5*(tp/(tp+fn)+tn/(tn+fp))-result['balanced_accuracy'])<1e-12
                assert abs(roc_auc_score(y,score)-result['auc'])<1e-12
                assert abs(np.mean(pred==1)-result['coverage'])<1e-12
                expected_bad=fp/(tp+fp) if tp+fp else np.nan
                assert (np.isnan(expected_bad) and np.isnan(result['bad_accepted'])) or abs(expected_bad-result['bad_accepted'])<1e-12
                local=[]
                for subject in np.unique(p['subject']):
                    mask=p['subject']==subject
                    counts=confusion_matrix(y[mask],pred[mask],labels=[0,1]).ravel()
                    saved=next(s for s in saved_subjects if s['subject_id']==subject)
                    assert counts.tolist()==[saved[k] for k in ['tn','fp','fn','tp']]
                    assert int(mask.sum())==saved['n']
                    local.append(counts)
                subject_counts[(task,spec['method'])]=np.array(local)
            hashes[str(path)]=sha(path);hashes[str(pfile)]=sha(pfile)
            for s in saved_subjects:subjects.append(dict(task=task,method=spec['method'],**s))
            for s in json.loads((folder/f'{stem}_strata.json').read_text()):strata.append(dict(task=task,method=spec['method'],**s))
            cost=result.pop('cost')
            if cost:
                for t in cost.pop('timings'):costs.append(dict(task=task,method=spec['method'],**t,**{k:v for k,v in cost.items() if k!='warnings'}))
                warnings+=cost.get('warnings',[])
            rows.append(result)
    assert len(rows)==84
    # Independent reconstruction of every window's occupancy from consensus intervals.
    intervals=read_frame(OUT/'consensus_intervals.csv')
    for name,g in windows.groupby('record_id'):
        c=intervals[intervals.record_id==name].sort_values('start_sample')
        a=c.start_sample.to_numpy();b=c.end_exclusive.to_numpy();v=c.quality.to_numpy()
        for w in g.itertuples():
            first=np.searchsorted(b,w.start_sample,side='right')
            last=np.searchsorted(a,w.end_sample,side='left')
            counts=np.zeros(4,int)
            for start,end,label in zip(a[first:last],b[first:last],v[first:last]):
                counts[label]+=min(end,w.end_sample)-max(start,w.start_sample)
            assert counts.sum()==10000 and counts[0]==0
            assert np.allclose(counts[1:]/10000,[w.class1_fraction,w.class2_fraction,w.class3_fraction])
            assert w.qrs_target==int(counts[3]==0) and w.morphology_target==int(counts[1]==10000)
    for fold in windows.fold.unique():
        assert not set(windows[windows.fold==fold].subject_id)&set(windows[windows.fold!=fold].subject_id)
    for path,digest in protocol['source_hashes'].items():assert sha(path)==digest,path
    # Repeated deterministic smoke windows must match full extraction features.
    smoke=read_frame(OUT/'smoke_features.csv').set_index('window_id')
    full=features.set_index('window_id').loc[smoke.index]
    compare=[c for c in smoke.select_dtypes('number') if not c.endswith('_seconds')]
    assert np.allclose(smoke[compare],full[compare],rtol=1e-12,atol=1e-12,equal_nan=True)
    results=pd.DataFrame(rows);per_subject=pd.DataFrame(subjects);per_stratum=pd.DataFrame(strata);costs=pd.DataFrame(costs)
    paired=[]
    for task in TASKS:
        for reference in [specs[1]['method'],specs[5]['method']]:
            for spec in specs:
                if spec['method']==reference:continue
                paired.append(dict(task=task,method=spec['method'],reference=reference,
                                   **paired_cluster_delta(subject_counts[(task,spec['method'])],subject_counts[(task,reference)])))
    pd.DataFrame(paired).to_csv(OUT/'paired_ba_differences.csv',index=False)
    save_json(OUT/'method_registry.json',dict(methods=specs,targets=TASKS,
        logistic=dict(max_iter=3000,class_weight='balanced',random_state=42),
        svm=dict(C=1.0,gamma='scale',kernel='rbf',class_weight='balanced',random_state=42),
        preprocessing='train-only SimpleImputer(median, keep_empty_features=True) and StandardScaler',
        stacking='3 subject-grouped inner folds, seed17; 5 outer folds seed42',
        thresholds='fixed gate thresholds unchanged across both targets; no test tuning'))
    results.to_csv(OUT/'metrics.csv',index=False);per_subject.to_csv(OUT/'per_subject_metrics.csv',index=False)
    per_stratum.to_csv(OUT/'per_stratum_metrics.csv',index=False);costs.to_csv(OUT/'learned_host_cost.csv',index=False)
    # Fixed rule timings include their detector stage when relevant.
    times={
        'bSQI EP+WQRS':features.ep_seconds+features.wqrs_seconds,
        'Orphanidou with EP':features.ep_seconds+features.orphanidou_seconds,
        'Kramer[ECGAssess]':features.kramer_seconds,
        'Zhao[NeuroKit]':features.zhao_seconds,
        'Ho[NeuroKit]':features.ho_seconds,
        'Clifford features + EP/WQRS':features.ep_seconds+features.wqrs_seconds+features.features_seconds,
        'All extraction':features.elapsed_seconds}
    latency=pd.DataFrame([dict(stage=name,p50_ms=float(np.median(t)*1000),p99_ms=float(np.quantile(t,.99)*1000),max_ms=float(np.max(t)*1000),n=len(t)) for name,t in times.items()])
    latency.to_csv(OUT/'feature_host_latency.csv',index=False)
    inventory=read_frame(OUT/'dataset_inventory.csv')
    sourcepaths=[Path(__file__),FW/'tools/run_butqdb_survey.py',FW/'tools/evaluate_butqdb_survey.py',FW/'tools/run_cinc2011_verified.py',
                 ARCHIVE/'clifford2012_reproduction/run_reproduction.py',ARCHIVE/'clifford2012_reproduction/compare_one_lead_categories.py',
                 ARCHIVE/'clifford2012_reproduction/external/eplimited/eplimited.dll',
                 ARCHIVE/'vendor/neurokit2_source/neurokit2/ecg/ecg_quality.py',
                 ARCHIVE/'vendor/neurokit2_source/neurokit2/signal/signal_quality.py',
                 FW/'.cache/butqdb_physionet.html',FW/'.cache/butqdb_100001_ANN.csv']
    hashes.update({str(p):sha(p) for p in sourcepaths})
    lines=['# Survey SQI trên BUTQDB — 20/10/2026','',
      '**Đây là tái lập các implementation đã khảo sát trên dataset mới, không phải khẳng định tái hiện số liệu của paper gốc.**',
      'Không dùng CinC2011 để suy ra nhãn một lead. BUTQDB trong lượt này có một kênh tên ECG, không giả định đó là MLII.', '',
      '## 1. Dataset và protocol','',
      f"Dataset local: `{DATA}`; mirror `epr-labs/butqdb`, revision `{protocol['hf_revision']}`.",
      f"18 records/15 subjects; {inventory.hours.sum():.2f} giờ ECG; {len(windows):,} cửa sổ10s có consensus đầy đủ; không thêm nhiễu nhân tạo.",
      'ECG1000Hz, physicalµV float16 trong mirror; chuyển float64/mV. ACC100Hz ×3 trục được kiểm kê nhưng không dùng làm feature trong survey ECG-only.',
      'Nhãn consensus annotator4. Đã đối chiếu toàn bộ763 interval consensus của100001 với CSV PhysioNet gốc: khớp chính xác. Start/end1-based inclusive chuyển thành[start-1,end).',
      'Cửa sổ10s, stride10s, grid cố định từ đầu record. Có bất kỳ sample0/unannotated: loại cửa sổ, không gán good/bad. Không lấy nhãn từ output detector.',
      'Class1: rõ hình thái; class2: vẫn đủ QRS nhưng không tin hình thái; class3: QRS không đáng tin. Target chính QRS:1+2 good,3 bad; target phụ morphology:1 good,2+3 bad.',
      'Mỗi window lấy lớp xấu nhất hiện diện, không lấy đa số; đây là policy thận trọng của dự án. Đính kèm fractions từng class và bảng pure/mixed để nhìn ảnh hưởng boundary.',
      f"Worst-class counts: {windows.quality_worst.value_counts().sort_index().to_dict()}; mixed-class windows:{int(windows.mixed_classes.sum())}.",
      f"Ba record full-annotation100001/105001/111001 chiếm {windows.record_id.isin(['100001','105001','111001']).mean():.2%} cửa sổ. Phải đọc cả micro/window-weighted và subject-macro metrics.",
      '5-fold StratifiedGroupKFold seed42, group=3 chữ số đầu tên record (subject), cùng folds cho mọi method/target. Imputation median/scaling chỉ fit train. Fixed rules giữ nguyên threshold. Fusion learned là mô hình dự án, fit train subjects. Stacking dùng inner3fold subject-disjoint.',
      'Resample polyphase1000→500→125Hz với1s waveform context thật mỗi phía rồi cắt về10s; line padding chỉ tại biên record. Không blanket-denoising trước SQI. Context không đưa vào nhãn/quality mask.', '',
      '[Nguồn mô tả BUTQDB và ý nghĩa nhãn](https://physionet.org/content/butqdb/1.0.0/). [Mirror được sử dụng](https://huggingface.co/datasets/epr-labs/butqdb/tree/891cd492b71ee796c05ae8398ed913614c66ca91).', '',
      table(inventory,['record_id','subject_id','hours','windows_total','windows_labeled','windows_unknown','class1_seconds','class2_seconds','class3_seconds']), '',
      '## 2. Method và mức fidelity','',
      '- Zhao[NeuroKit]: gọi trực tiếp0.2.13 ecg_quality(zhao2018, approach=simple), accept Barely/Excellent. Library không có qSQI trong implementation này; không gọi nó là Zhao-fuzzy nguyên bản.',
      '- Ho[NeuroKit]: gọi trực tiếp ho2025 (UNSW+NeuroKit), mean quality mask theo sample; thresholds80/90/100% là policy window của dự án, không phải AF classifier.',
      '- Orphanidou: rule/template-CC threshold0.66, EP detector adaptation125Hz; không phải exact tác giả original detector.',
      '- bSQI: EP Limited C + py-ecg-detectors WQRS, match50ms,2matches/(n1+n2). EP dùng warm-up unscored trên cùng window trước scored pass; đo offline, không gọi là causal streaming.',
      '- Kramer: implementation ECGAssess rules của survey trước,500Hz. Không thay detector hay threshold theo BUTQDB.',
      '- Clifford-5/6 và b+p+k+bas: feature subsets + Logistic/RBF-SVM của dự án. Không phải bản12-lead/private-test paper. Không có Clifford84/iSQI cross-lead vì dataset single-lead. Không có Zhao-6.',
      '- Cascade giữ cutoffs bSQI0.96/1.0 từ experiment trước; stage giữa Logistic fit train only. Không tối ưu cutoff trên BUTQDB test.',
      '- Logistic score stacking dùng cross-fit theo subject trong train. Tất cả learned method có mô hình riêng theo target, không chuyển nhãn morphology thành nhãn QRS.', '',
      '## 3. Định nghĩa metric','',
      'Positive=usable. BA=(good recall+bad recall)/2. Coverage=accepted/all labeled windows. Bad accepted=bad/accepted. Bad pass rate=accepted bad/all bad. AUC SVM dùng decision_function; Zhao categorical score0/0.5/1.',
      '95% CI BA:bootstrap2000 lần theo subject trên predictions OOF cố định, không refit model trong từng bootstrap và không bootstrap từng window. CI này không bao trùm toàn bộ bất định do huấn luyện. Macro BA chỉ trung bình subject có cả2 class; ghi rõ số subject hợp lệ. Không xếp hạng dựa riêng một metric khi coverage khác nhau.', '']
    for task in TASKS:
        lines+=['## '+task,'']
        for family,g in results[results.task==task].groupby('family',sort=False):
            lines+=['### '+family,'',table(g,['method','balanced_accuracy','auc','coverage','bad_accepted','bad_pass_rate','ba_ci_low','ba_ci_high','subject_macro_ba_both_classes','subjects_with_both_classes']),'']
    lines+=['## 4. Thời gian và dung lượng','',
      'Host Python/C DLL trong run4process; số thời gian dưới đây không phải latency ESP32 và có scheduling contention. Có bao gồm detector cho Orphanidou/bSQI. Không gán host RAM thành RAM MCU.', '',
      table(latency,['stage','n','p50_ms','p99_ms','max_ms']), '',
      'Chi phí learned stage từng fold: [learned_host_cost.csv](learned_host_cost.csv). Pickle bytes chỉ là artifact host; p99 từ50 lời gọi score một sample trên mỗi outer-fold model, không gồm feature extraction/stack base-stage. tracemalloc batch128 không bao quát native memory.',
      'FlashΔ/RAMΔ mới trên ESP32: chưa đo; không tự gán0. Chi phí hardware cũ ở report trước không được đổi tên thành BUTQDB on-device benchmark.', '',
      '## 5. Kết quả phụ và kiểm chứng','',
      '[Paired BA differences](paired_ba_differences.csv): mỗi method so với Orphanidou và Ho[NeuroKit]80%, dùng cùng subject bootstrap draw cho hai phía. CI delta chứa0 không chứng minh cải thiện BA; CI exploratory, chưa hiệu chỉnh multiple comparisons. Không chọn threshold từ các CI này.',
      '[Method registry và hyperparameters](method_registry.json). Fixed gates giữ cùng acceptance policy cho cả hai target, ví dụ Zhao accept Barely/Excellent; target phụ không tự đổi cutoff theo test labels.',
      '[Per-subject](per_subject_metrics.csv); [pure/mixed và worst-class strata](per_stratum_metrics.csv); [dataset inventory](dataset_inventory.csv); [window labels/folds](windows.csv); [protocol/hash](protocol.json).',
      'Predictions cho mỗi method/target được lưu trong qrs_target/ và morphology_target/ dưới dạng NPZ (window_id,target,prediction,score,fold,subject). Không drop cửa sổ khi method lỗi; exception phải dừng extraction.',
      f"Warnings trích xuất: {int((features.warnings!='[]').sum())} windows; chi tiết trong features.csv. Fit warnings:{sorted(set(warnings))}.",
      'Audit độc lập: tính lại tất cả84 bảng từ predictions; kiểm tra mọi window against consensus intervals; train/test subject disjoint; source-data hash;92 smoke windows khớp full features.', '',
      '## 6. Giới hạn','',
      '- Mirror float16 đã lượng hóa waveform so với WFDB gốc; không tuyên bố bit-exact data reproduction của PhysioNet.',
      '- Một lớp3 ngắn cũng làm window thành bad theo policy worst-class. Không suy diễn BA đó thành khả năng phát hiện AF hay mọi RR đều sai.',
      '- BUTQDB quality annotation không phải R-peak ground truth và không phải AF rhythm label. Vì vậy không báo accuracy BPM/AF supervised trên bộ này.',
      '- Chỉ15 subjects và3 records full-label chiếm phần lớn windows; CI subject-level có thể rộng. Các model learned được huấn luyện lại trong groupedCV, không phải frozen external-transfer test từ CinC.',
      '- Không có test phần cứng mới hoặc clinical validation.', '',
      '## 7. Tái chạy','',
      '```powershell',r'$env:PYTHONDONTWRITEBYTECODE="1"',
      r'..\..\.venv\Scripts\python.exe tools\run_butqdb_survey.py --phase prepare',
      r'..\..\.venv\Scripts\python.exe tools\run_butqdb_survey.py --phase smoke --workers 4',
      r'..\..\.venv\Scripts\python.exe tools\run_butqdb_survey.py --phase extract --workers 4',
      r'..\..\.venv\Scripts\python.exe tools\evaluate_butqdb_survey.py --task qrs_target',
      r'..\..\.venv\Scripts\python.exe tools\evaluate_butqdb_survey.py --task morphology_target',
      r'..\..\.venv\Scripts\python.exe tools\report_butqdb_survey.py','```','',
      'Chỉ resume cùng protocol/source; khi đổi thuật toán hoặc nhãn, dùng output directory mới. pyarrow được cài riêng vào .cache/butqdb_deps; NeuroKit dùng vendor0.2.13 đã pin.']
    (OUT/'REPORT.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
    hashes.update({str(p):sha(p) for p in OUT.glob('*.csv')});hashes[str(OUT/'REPORT.md')]=sha(OUT/'REPORT.md')
    save_json(OUT/'final_audit.json',dict(passed=True,methods=42,tasks=2,metric_rows=84,windows=len(windows),
        every_window_label_reconstructed=True,subject_disjoint=True,source_data_hashes_match=True,
        smoke_full_parity=True,all_metric_confusions_auc_recomputed=True,
        versions=dict(python=platform.python_version(),numpy=np.__version__,pandas=pd.__version__,scipy=scipy.__version__,sklearn=sklearn.__version__,neurokit='0.2.13'),
        hashes=hashes))
    print('COMPLETE audited',OUT,flush=True)

if __name__=='__main__':main()
