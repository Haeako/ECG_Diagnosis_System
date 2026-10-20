"""Build auditable report from complete subject-disjoint BUTQDB experiments."""
import json
import platform
import sys
from pathlib import Path
# Configure the isolated pyarrow dependency before pandas imports optional engines.
from run_butqdb_survey import OUT,DATA,ARCHIVE,FW,read_frame,save_json,sha
import numpy as np
import pandas as pd
import scipy
import sklearn
from sklearn.metrics import confusion_matrix,roc_auc_score
from evaluate_butqdb_survey import TASKS,method_specs

ARCHIVED_FAMILIES={'project score fusion','project stacked fusion'}

def main_report_mask(frame):
    """Presentation scope only; never alter predictions or historical metrics."""
    return (frame.task=='qrs_target') & ~frame.family.isin(ARCHIVED_FAMILIES)

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
    original=pd.read_parquet(DATA/'data/train-00000-of-00001.parquet')
    interval_total=0
    for record in original.itertuples():
        name=json.loads(record.ecg_header)['record_name'].split('_')[0]
        annotations=pd.DataFrame(record.annotations)
        consensus=annotations[annotations.annotator==4].sort_values('start')
        saved=intervals[intervals.record_id==name].sort_values('start_sample')
        expected=np.column_stack([consensus.start.to_numpy()-1,consensus.end,consensus.value]).astype(np.int64)
        assert np.array_equal(expected,saved[['start_sample','end_exclusive','quality']].to_numpy()),name
        interval_total+=len(consensus)
    assert interval_total==len(intervals)
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
    paired=pd.DataFrame(paired)
    paired.to_csv(OUT/'paired_ba_differences.csv',index=False)
    save_json(OUT/'method_registry.json',dict(methods=specs,targets=TASKS,
        logistic=dict(max_iter=3000,class_weight='balanced',random_state=42),
        svm=dict(C=1.0,gamma='scale',kernel='rbf',class_weight='balanced',random_state=42),
        preprocessing='train-only SimpleImputer(median, keep_empty_features=True) and StandardScaler',
        stacking='3 subject-grouped inner folds, seed17; 5 outer folds seed42',
        thresholds='fixed gate thresholds unchanged across both targets; no test tuning'))
    results.to_csv(OUT/'metrics.csv',index=False);per_subject.to_csv(OUT/'per_subject_metrics.csv',index=False)
    per_stratum.to_csv(OUT/'per_stratum_metrics.csv',index=False);costs.to_csv(OUT/'learned_host_cost.csv',index=False)
    main_results=results[main_report_mask(results)].copy()
    archived_results=results[~main_report_mask(results)].copy()
    assert len(main_results)==28 and len(archived_results)==56
    main_names=set(main_results.method)
    main_results.to_csv(OUT/'metrics_qrs.csv',index=False)
    for source,name in [(per_subject,'per_subject_metrics_qrs'),(per_stratum,'per_stratum_metrics_qrs'),
                        (costs,'learned_host_cost_qrs'),(paired,'paired_ba_differences_qrs')]:
        source[(source.task=='qrs_target')&source.method.isin(main_names)].to_csv(OUT/f'{name}.csv',index=False)
    save_json(OUT/'method_registry_qrs.json',dict(
        methods=[s for s in specs if s['method'] in main_names],target='qrs_target',
        model_settings='See method_registry.json for unchanged historical hyperparameters',
        scope='28 configurations; excluded results preserved in streck.md; no refit or threshold changes'))
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
    metric_columns=['method','balanced_accuracy','auc','coverage','bad_accepted','bad_pass_rate','ba_ci_low','ba_ci_high','subject_macro_ba_both_classes','subjects_with_both_classes']
    archive_lines=['# streck — kết quả đưa ra khỏi báo cáo chính','',
      'Theo yêu cầu thu gọn về QRS, file này giữ toàn bộ42 kết quả morphology và14 kết quả QRS thuộc score fusion/stacking. Không xóa predictions, không đổi nhãn, không chạy lại model. Không dùng56 hàng này để chọn phương pháp trong báo cáo chính.','',
      '## Định nghĩa của các thử nghiệm lưu trữ','',
      '- Morphology target: toàn cửa sổ class1 là good; có bất kỳ class2/3 là bad. QRS target vẫn nhận class1+2. Learned models được fit riêng theo target; fixed gates giữ nguyên acceptance policy.',
      '- Score fusion:6 cặp giữa bSQI/Orphanidou/Zhao/Kramer × Logistic hoặc RBF-SVM =12 cấu hình mỗi target.',
      '- Stacking: Project-5SQI Logistic tạo base score bằng3 inner folds tách subject. Tầng Logistic thứ hai nhận base score + bSQI, hoặc base score + cả4 score bSQI/Orphanidou/Zhao/Kramer.',
      '- Project-5SQI nền đã có bSQI; đưa bSQI trực tiếp vào tầng hai là thử nghiệm thông tin dư thừa, không phải một paper độc lập. Hai tầng không dùng test labels để fit.',
      '- BASE5: bSQI F1, spectral entropy, normalized-difference RMS, median-template correlation, RR-outlier fraction.',
      '- Tất cả dùng cùng35.773 cửa sổ,5 outer folds theo subject. Các tỷ lệ0–1; định nghĩa metric và preprocessing chung ở [báo cáo chính](REPORT.md).','',
      '## Bảng kết quả đã chuyển khỏi báo cáo chính','']
    for task in TASKS:
        archive_lines+=['### '+task,'']
        for family,g in archived_results[archived_results.task==task].groupby('family',sort=False):
            archive_lines+=['#### '+family,'',table(g,metric_columns),'']
    archive_lines+=['## Artifact gốc để truy vết','',
      '[84 metrics gốc](metrics.csv), [registry đủ42 cấu hình](method_registry.json), [paired differences đủ2 target](paired_ba_differences.csv), [per-subject](per_subject_metrics.csv), [per-stratum](per_stratum_metrics.csv), [chi phí learned](learned_host_cost.csv).',
      'Predictions vẫn nằm trong qrs_target/ và morphology_target/. Các JSON/NPZ từng cấu hình không bị di chuyển hoặc thay đổi. protocol.json giữ nguyên mô tả lần chạy gốc2 target.',
      'Chi phí stacking chỉ đo tầng cuối khi inference; không coi đó là latency toàn pipeline. Báo cáo chính không sử dụng số này.',
      'Để tái chạy phần morphology đã lưu trữ: `..\\..\\.venv\\Scripts\\python.exe tools\\evaluate_butqdb_survey.py --task morphology_target` từ thư mục firmware.']
    (OUT/'streck.md').write_text('\n'.join(archive_lines)+'\n',encoding='utf-8')
    lines=['# Survey chất lượng QRS trên BUTQDB — 20/10/2026','',
      'Báo cáo chính chỉ trả lời: cửa sổ ECG10 giây có đủ chất lượng để sử dụng QRS hay không? Giữ28 cấu hình: baseline, fixed gates, SQI đơn, feature fusion và cascade.',
      'Các phần ngoài phạm vi được chuyển sang [streck.md](streck.md). Đây là thay đổi phạm vi trình bày, không thay đổi kết quả đã chạy.','',
      '**Đây là tái lập các implementation đã khảo sát trên dataset mới, không phải khẳng định tái hiện số liệu của paper gốc.**',
      'Không dùng CinC2011 để suy ra nhãn một lead. BUTQDB trong lượt này có một kênh tên ECG, không giả định đó là MLII.', '',
      '## 1. Dataset và protocol','',
      f"Dataset local: `{DATA}`; mirror `epr-labs/butqdb`, revision `{protocol['hf_revision']}`.",
      f"18 records/15 subjects; {inventory.hours.sum():.2f} giờ ECG; {len(windows):,} cửa sổ10s có consensus đầy đủ; không thêm nhiễu nhân tạo.",
      'ECG1000Hz, physicalµV float16 trong mirror; chuyển float64/mV. ACC100Hz ×3 trục được kiểm kê nhưng không dùng làm feature trong survey ECG-only.',
      'Nhãn consensus annotator4. Đã đối chiếu toàn bộ763 interval consensus của100001 với CSV PhysioNet gốc: khớp chính xác. Start/end1-based inclusive chuyển thành[start-1,end).',
      'Cửa sổ10s, stride10s, grid cố định từ đầu record. Có bất kỳ sample0/unannotated: loại cửa sổ, không gán good/bad. Không lấy nhãn từ output detector.',
      'Target QRS: class1+2 là good (đủ để sử dụng QRS); class3 là bad (QRS không đáng tin).',
      'Mỗi window lấy lớp xấu nhất hiện diện, không lấy đa số; đây là policy thận trọng của dự án. Đính kèm fractions từng class và bảng pure/mixed để nhìn ảnh hưởng boundary.',
      f"Worst-class counts: {windows.quality_worst.value_counts().sort_index().to_dict()}; mixed-class windows:{int(windows.mixed_classes.sum())}.",
      f"Ba record full-annotation100001/105001/111001 chiếm {windows.record_id.isin(['100001','105001','111001']).mean():.2%} cửa sổ. Phải đọc cả micro/window-weighted và subject-macro metrics.",
      '5-fold StratifiedGroupKFold seed42, group=3 chữ số đầu tên record (subject), cùng folds cho mọi cấu hình. Imputation median/scaling chỉ fit train. Fixed rules giữ nguyên threshold. Feature fusion learned là mô hình dự án, fit train subjects.',
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
      '- Feature fusion đưa các đặc trưng trực tiếp vào Logistic/SVM. Giữ nhóm này và cascade; chỉ các nhóm được chỉ định loại khỏi phạm vi mới được chuyển sang streck.md.', '',
      '### Feature definitions của implementation đang chạy','',
      '- pSQI=P(5–15Hz)/P(5–40Hz); basSQI=1−P(0–1Hz)/P(0–40Hz). Power dùng periodogram trên waveform đã trừ mean,125Hz.',
      '- sSQI=skewness bias=False; kSQI=Pearson kurtosis (fisher=False,bias=False). Không lấy trị tuyệt đối của skewness.',
      '- fSQI=tỷ lệ cặp sample liền nhau có chênh lệch <25µV ở125Hz. Đây là feature flatline của implementation, không tự coi score cao là good.',
      '- bSQI F1 dùng EP+WQRS, tolerance6samples tại125Hz (48ms sau làm tròn từ50ms). Wrapper EP resample nội bộ125→200Hz trước gọi DLL rồi map peak về125Hz.',
      '- Project-5SQI: bSQI F1, spectral entropy, normalized-difference RMS, median-template correlation, RR-outlier fraction. Đây không phải Clifford-5.',
      '- Feature bsqi lịch sử150ms còn trong features.csv để truy vết nhưng không được dùng bởi28 cấu hình trong báo cáo chính; registry ghi rõ columns.', '',
      '## 3. Định nghĩa metric','',
      'Positive=usable. BA=(good recall+bad recall)/2. Coverage=accepted/all labeled windows. Bad accepted=bad/accepted. Bad pass rate=accepted bad/all bad. AUC SVM dùng decision_function; Zhao categorical score0/0.5/1.',
      '95% CI BA:bootstrap2000 lần theo subject trên predictions OOF cố định, không refit model trong từng bootstrap và không bootstrap từng window. CI này không bao trùm toàn bộ bất định do huấn luyện. Macro BA chỉ trung bình subject có cả2 class; ghi rõ số subject hợp lệ. Không xếp hạng dựa riêng một metric khi coverage khác nhau.',
      'Các tỷ lệ trong bảng dùng thang0–1. AUC learned là pooled OOF; decision scores từ các SVM fit riêng từng fold chưa calibration nên cần thận trọng khi so AUC xuyên fold.', '']
    lines+=['## Kết quả QRS','']
    for family,g in main_results.groupby('family',sort=False):
        lines+=['### '+family,'',table(g,metric_columns),'']
    lines+=['## 4. Thời gian và dung lượng','',
      'Feature extraction chạy4process; hai job learned-target chạy song song. Số thời gian host Python/C DLL dưới đây không phải latency ESP32 và có scheduling contention. Có bao gồm detector cho Orphanidou/bSQI. Không gán host RAM thành RAM MCU.', '',
      table(latency,['stage','n','p50_ms','p99_ms','max_ms']), '',
      'Chi phí learned stage từng fold: [learned_host_cost_qrs.csv](learned_host_cost_qrs.csv). Pickle bytes chỉ là artifact host; p99 từ50 lời gọi score một sample trên mỗi outer-fold model, không gồm feature extraction. tracemalloc batch128 không bao quát native memory.',
      'FlashΔ/RAMΔ mới trên ESP32: chưa đo; không tự gán0. Chi phí hardware cũ ở report trước không được đổi tên thành BUTQDB on-device benchmark.', '',
      '## 5. Cách kết luận trong đúng pipeline này','',
      '### Đang kiểm tra điều gì?','',
      'Pipeline benchmark: ECG BUTQDB → cửa sổ10s → preprocessing → score/features → gate good/bad → đối chiếu consensus class1+2/class3. Learned gates được fit trên train subjects và dự đoán subject thuộc fold test. Không chạy AF, không dùng R-peak annotation để đo sai số BPM.',
      'Ví dụ kết luận có số liệu: Ho80 có BA0,9179 so với bSQI0,8 là0,8215; Ho giữ đúng86,21% cửa sổ good so với65,21%. Đổi lại bad accepted là0,58% so với0,27%. Với mục tiêu giữ good đồng thời loại bad, Ho80 là lựa chọn tốt hơn cặp bSQI0,8 trong lần đánh giá này.','',
      '### “Thắng do ngẫu nhiên” nằm ở bước chọn phương pháp','',
      'Ở đây không nói model đoán ngẫu nhiên mỗi lần chạy. Cùng dữ liệu và seed, predictions có thể lặp lại. Vấn đề là chỉ có15 người: một cấu hình có thể hợp với những người đang có hơn là với quần thể người dùng.',
      'Nếu nhìn nhiều cấu hình/ngưỡng trên cùng tập đánh giá rồi chọn cấu hình có điểm cao nhất, ta có thể chọn đúng cấu hình hưởng lợi nhiều nhất từ đặc điểm tình cờ của tập người này. Đây là sai lệch ở bước lựa chọn pipeline từ bảng kết quả, không phải lỗi phép tính BA.',
      '[So sánh BA theo từng cặp](paired_ba_differences_qrs.csv) lấy mẫu lại cùng danh sách subject cho hai phương pháp. Delta=BA(method)−BA(reference). Khoảng chứa0 nghĩa là dữ liệu chưa tách rõ trường hợp hơn/kém; không có nghĩa hai method bằng nhau.',
      'Ví dụ Ho80−bSQI0,8: +9,64 điểm phần trăm; khoảng95% [+2,70;+13,01]. Đây là bằng chứng theo cặp nghiêng về Ho. Các khoảng được tính riêng từng cặp, chưa điều chỉnh chung cho việc xem nhiều cặp; không dùng chúng để tuyên bố đã chứng minh người thắng trong toàn bộ28 cấu hình.',
      'Ngưỡng phải được chọn trên train/validation, rồi khóa trước khi chấm test subjects. Không xem test labels hoặc các CI của test để sửa ngưỡng và báo lại trên chính test đó như một kiểm chứng độc lập. Lần chạy hiện tại giữ ngưỡng có sẵn; không tối ưu ngưỡng từ bảng kết quả.','',
      '### Kiểm tra tính đúng của lần chạy khác với đo chất lượng gate','',
      '- Kiểm toán trả lời “có gán đúng nhãn, tách đúng người, tính đúng số và dùng đúng dữ liệu không?”. Một gate luôn báo good vẫn có thể qua kiểm toán vì tính toán của nó đúng, dù BA chỉ0,5. No gate trong bảng là ví dụ cụ thể.',
      '- Hiệu quả gate trả lời bằng BA, coverage, good recall và bad accepted đo từ predictions. Kiểm toán không thay thế các metric này.',
      '- Reproduction paper là một câu hỏi khác: có khớp thuật toán, detector, dữ liệu và protocol của nghiên cứu gốc không? Orphanidou ở đây dùng detector EP adaptation; Clifford là feature subset single-lead; Zhao là implementation NeuroKit simple. Vì vậy báo cáo này là so sánh implementation trên BUTQDB, không tự gọi là tái hiện đầy đủ paper gốc.','',
      '[28 metrics QRS](metrics_qrs.csv), [registry QRS](method_registry_qrs.json), [per-subject QRS](per_subject_metrics_qrs.csv), [pure/mixed QRS](per_stratum_metrics_qrs.csv), [dataset inventory](dataset_inventory.csv), [window labels/folds](windows.csv). protocol.json giữ manifest lần chạy gốc để truy vết; phạm vi báo cáo hiện tại được ghi trong final_audit.json.',
      'Predictions QRS được lưu trong qrs_target/ dưới dạng NPZ (window_id,target,prediction,score,fold,subject). Không drop cửa sổ khi method lỗi; exception phải dừng extraction.',
      f"Warnings trích xuất: {int((features.warnings!='[]').sum())} windows; chi tiết trong features.csv. Fit warnings:{sorted(set(warnings))}.",
      'Cảnh báo không đồng nghĩa135 cửa sổ bị loại: vẫn chấm đủ35.773 cửa sổ. Có16 giá trị RR-outlier và42 giá trị template correlation không xác định; learned pipeline điền median từ train, không lấy median từ test.',
      'Audit: tính lại confusion/BA/AUC/coverage/bad accepted của toàn bộ84 kết quả gốc, trong đó28 hàng thuộc báo cáo này và56 hàng đã lưu trữ; đối chiếu consensus với parquet và mọi window với intervals; kiểm tra train/test không chung subject; hash dữ liệu;92 smoke windows khớp full features.', '',
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
      r'..\..\.venv\Scripts\python.exe tools\report_butqdb_survey.py','```','',
      'Chỉ resume cùng protocol/source; khi đổi thuật toán hoặc nhãn, dùng output directory mới. pyarrow được cài riêng vào .cache/butqdb_deps; NeuroKit dùng vendor0.2.13 đã pin.',
      'Runner đánh giá giữ nguyên registry lịch sử; generator báo cáo kiểm toán cả artifact đã lưu trữ rồi chỉ xuất28 hàng QRS vào báo cáo chính. Đổi phạm vi trình bày không xóa hoặc fit lại artifact cũ.']
    (OUT/'REPORT.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
    hashes.update({str(p):sha(p) for p in OUT.glob('*.csv')});hashes[str(OUT/'REPORT.md')]=sha(OUT/'REPORT.md')
    for p in [OUT/'protocol.json',OUT/'method_registry.json',OUT/'method_registry_qrs.json',OUT/'streck.md',FW/'tools/test_butqdb_survey.py']:
        hashes[str(p)]=sha(p)
    save_json(OUT/'final_audit.json',dict(passed=True,methods=42,tasks=2,metric_rows=84,windows=len(windows),
        reporting_scope=dict(main_target='qrs_target',main_metric_rows=28,archived_metric_rows=56,
                             excluded_families=sorted(ARCHIVED_FAMILIES),archive='streck.md',
                             raw_predictions_unchanged=True),
        consensus_intervals_match_source_parquet=True,
        every_window_label_reconstructed=True,subject_disjoint=True,source_data_hashes_match=True,
        smoke_full_parity=True,all_metric_confusions_auc_recomputed=True,
        versions=dict(python=platform.python_version(),numpy=np.__version__,pandas=pd.__version__,scipy=scipy.__version__,sklearn=sklearn.__version__,neurokit='0.2.13'),
        hashes=hashes))
    print('COMPLETE audited',OUT,flush=True)

if __name__=='__main__':main()
