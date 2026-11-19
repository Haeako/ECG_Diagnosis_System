"""Render the requested compact latest-only summary from complete audited runs."""
import hashlib
import json
from pathlib import Path
import pandas as pd
import numpy as np
from sklearn.metrics import roc_auc_score

ROOT = Path(__file__).resolve().parents[2]


def checked(run, filename, audit_name, hash_key):
    folder = ROOT/run
    audit = json.loads((folder/audit_name).read_text())
    assert audit['status'].startswith('PASS')
    data = (folder/filename).read_bytes()
    assert hashlib.sha256(data).hexdigest() == audit[hash_key]
    return pd.read_csv(folder/filename)


def refresh(verification=None):
    bpm = checked('outputs/BPM/c_module_window_matrix_20261116', 'overview.csv', 'publication_audit.json', 'overview_sha256')
    esp = checked('outputs/BPM/esp32_latency_20261117', 'summary.csv', 'module_audit.json', 'summary_sha256')
    sqi = checked('outputs/SQI/survey_additional_full_20261118', 'published_summary.csv', 'publication_audit.json', 'published_summary_sha256')
    af = checked('outputs/AFib/afdb_frozen_full_20261118', 'verified_metrics.csv', 'publication_audit.json', 'verified_metrics_sha256')
    bpm = bpm.merge(esp, on='method', validate='one_to_one').sort_values('mean_condition_BPM_RMSE')
    assert len(bpm)==12 and bpm.complete_conditions.eq(22).all() and bpm.status.eq('COMPLETE').all()
    assert bpm.cases.eq(66).all() and bpm.pass_cases.eq(66).all()
    qrs=checked('outputs/BPM/c_qrs_tolerance_20261118','summary.csv','publication_audit.json','summary_sha256')
    assert len(qrs)==24 and set(qrs.tolerance_ms)=={50,150}
    qrs_f1=qrs.pivot(index='method',columns='tolerance_ms',values='F1').rename(columns={50:'F1_50',150:'F1_150'})
    bpm=bpm.merge(qrs_f1,on='method',validate='one_to_one')
    assert len(bpm)==12 and bpm[['F1_50','F1_150']].notna().all().all()
    # Full-record server implementations have their own evaluation scope.
    manifest=json.loads((ROOT/'visualize/results/BPM/host_rmse_latency/manifest.json').read_text())
    assert manifest['status']=='PASS'
    server_sources=[
        'outputs/BPM/edb_holdout_matrix_20261111/verified_complete_cells.csv',
        'outputs/BPM/edb_wim_matrix_20261113/full/verified_complete_cells.csv',
        'outputs/BPM/panpp_fixed_edb_20261115/verified_complete_cells.csv',
    ]
    server_frames=[]
    for relative in server_sources:
        digest=manifest['source_sha256'][relative.replace('/', '\\')]
        assert hashlib.sha256((ROOT/relative).read_bytes()).hexdigest()==digest
        server_frames.append(pd.read_csv(ROOT/relative))
    # Replace the old Pan++ result with the independently audited corrected run.
    server_frames[0]=server_frames[0][server_frames[0].method.ne('PanTompkinsPlusPlus_2022')]
    server_cells=pd.concat(server_frames,ignore_index=True)
    conditions={'original'}|{f'{n}_{s}' for n in ('bw','ma','em') for s in (-6,0,6,12,18,24)}|{f'{n}_mixed' for n in ('bw','ma','em')}
    server_rows=[]
    for method,g in server_cells.groupby('method'):
        if len(g)!=44 or any(set(g[g.tolerance_ms.eq(t)].condition)!=conditions for t in (50,150)):
            continue
        assert g.records.eq(57).all() and not g.duplicated(['condition','tolerance_ms']).any()
        tight=g[g.tolerance_ms.eq(50)].set_index('condition').sort_index()
        wide=g[g.tolerance_ms.eq(150)].set_index('condition').sort_index()
        np.testing.assert_allclose(tight[['bpm_mae','bpm_rmse','bpm_coverage']],wide[['bpm_mae','bpm_rmse','bpm_coverage']])
        assert g[['bpm_mae','bpm_rmse','f1','bpm_coverage']].notna().all().all()
        server_rows.append(dict(method=method,mae=wide.bpm_mae.mean(),rmse=wide.bpm_rmse.mean(),
            f1_50=tight.f1.mean(),f1_150=wide.f1.mean(),coverage=wide.bpm_coverage.min()))
    assert {'SWT_SepConv_model','ECG2RR_LSTM','firmware_C'}<=set(r['method'] for r in server_rows)
    assert len(sqi)==4 and sqi.windows.eq(35773).all() and sqi.common_valid_windows.eq(30986).all()
    af = af[af.record.eq('pooled') & af.cohort.eq('common')].sort_values('ba', ascending=False)
    assert len(af)==7 and af.n.eq(18470).all()
    fusion=checked('outputs/AFib/fusion_lr_full_shdb_20261118','verified_metrics.csv','publication_audit.json','verified_metrics_sha256')
    assert len(fusion)==1 and fusion.n.eq(18470).all()
    af=pd.concat([af,fusion],ignore_index=True).sort_values('ba',ascending=False)
    # Recompute a common valid cohort across all nine complete SQI scorers.
    base = ROOT/'outputs/SQI'
    audit = json.loads((ROOT/'outputs/host_sqi_af_20261114/publication_audit.json').read_text())
    assert audit['status']=='PASS'
    frames = {}
    times = {}
    scopes = {}
    for task, timer, title in [('ho','provider_seconds','Ho'),('bsqi','host_seconds','bSQI C'),('kramer','host_call_seconds','Kramer C'),('shared_peaks','combined_host_seconds','shared')]:
        path=base/f'{task}_host_20261114/predictions.csv'
        entry=next(x for x in audit['sqi_timings'] if x['task']==task)
        assert hashlib.sha256(path.read_bytes()).hexdigest()==entry['predictions_sha256']
        data=pd.read_csv(path)
        for method,g in data.groupby('method') if 'method' in data else [(title,data)]:
            g=g.set_index('window_id')
            frames[method]=g.score
            t=g[timer]+g.kernel_seconds if task=='ho' else g[timer]
            times[method]=t*1000
            scopes[method]={'ho':'provider + kernel','bsqi':'XQRS + SQI*','kramer':'native waveform','shared_peaks':'hai gate chung*'}[task]
    labels=pd.read_csv(base/'ho_host_20261114/predictions.csv').set_index('window_id').qrs_target
    path=base/'survey_additional_full_20261118/predictions.csv'
    a=json.loads((path.parent/'publication_audit.json').read_text())
    assert hashlib.sha256(path.read_bytes()).hexdigest()==a['predictions_sha256']
    for method,g in pd.read_csv(path,low_memory=False).groupby('method'):
        g=g.set_index('window_id')
        frames[method]=g.score
        times[method]=(g.shared_seconds+g.quality_seconds)*1000
        scopes[method]='preprocess + R + SQI'
    score=pd.DataFrame(frames).reindex(labels.index)
    assert score.shape==(35773,9)
    common=np.isfinite(score).all(axis=1)
    sqi_rows=[]
    for method in score:
        t=times[method]
        sqi_rows.append(dict(method=method,auc=roc_auc_score(labels[common],score.loc[common,method]),auc_own=roc_auc_score(labels[np.isfinite(score[method])],score.loc[np.isfinite(score[method]),method]),coverage=np.isfinite(score[method]).mean(),p50=t.quantile(.5),p99=t.quantile(.99),scope=scopes[method]))
    names = {
        'firmware_C':'Firmware C (baseline)', 'SWT_SepConv_model':'SWT-SepConv',
        'ECG2RR_LSTM':'ECG2RR LSTM', 'XQRS_WFDB':'XQRS / WFDB', 'GQRS_WFDB':'GQRS / WFDB',
        'WIM_xia':'WIM Xia',
        'FastQRS_0.3.0':'Fast-QRS', 'PanTompkinsPlusPlus_2022':'Pan–Tompkins++',
        'PanTompkins_pyecgdetectors':'Pan–Tompkins', 'Elgendi_pyecgdetectors':'Elgendi',
        'WIM_nguyen':'WIM Nguyen', 'WIM_elgendi':'WIM Elgendi', 'WIM_pantompkins':'WIM Pan–Tompkins',
        'WIM_hamilton':'WIM Hamilton', 'WIM_arteagaFalconi':'WIM Arteaga-Falconi',
        'WIM_shaik':'WIM Shaik', 'WIM_xu':'WIM Xu', 'WIM_zhai':'WIM Zhai',
        'templatematch':'Template matching', 'dissimilarity':'Dissimilarity',
        'zhao_fuzzy':'Zhao fuzzy', 'averageQRS':'averageQRS',
        'logistic_regression_rr':'Logistic Regression RR', 'random_forest_rr':'Random Forest RR',
        'extra_trees_float_rr':'Extra Trees float RR', 'extra_trees_int8_rr':'Extra Trees INT8 RR',
        'xgboost_rr':'XGBoost RR', 'convnext':'ConvNeXtV2', 'ecgfounder':'ECGFounder', 'fusion_lr_full_shdb':'ECGFounder + Extra Trees INT8 RR + LR',
    }
    lines = ['# Tổng kết benchmark mới nhất', '',
        'Cập nhật 19/11/2026. Chỉ giữ các lượt chạy đủ cohort và đã đối soát.', '',
        '## BPM', '',
        'EDB: 57 record, lead 0; 22 điều kiện: Original; BW/MA/EM × 6 SNR (−6, 0, 6, 12, 18, 24 dB); BW/MA/EM mixed (mỗi burst trộn sáu mức). Accuracy là bản C chạy trên host, cửa sổ 10 giây, bước 5 giây. MAE/RMSE dưới đây là trung bình đều theo điều kiện; coverage là giá trị thấp nhất trong 22 điều kiện.', '',
        '| Phương pháp C | MAE (BPM) | RMSE (BPM) | F1 R-peak ±50 ms % | F1 R-peak ±150 ms % | Coverage thấp nhất % | ESP32 p50 (ms/10s ECG) | Workspace (KiB) |',
        '|---|---:|---:|---:|---:|---:|---:|---:|']
    for row in bpm.to_dict('records'):
        lines.append(f"| {names[row['method']]} | {row['mean_condition_BPM_MAE']:.3f} | {row['mean_condition_BPM_RMSE']:.3f} | {100*row['F1_50']:.2f} | {100*row['F1_150']:.2f} | {100*row['min_condition_coverage']:.3f} | {row['latency_p50_ms']:.3f} | {row['workspace_bytes']/1024:.2f} |")
    lines += ['', 'ESP32-D0WD-V3, 160 MHz: 66 đoạn × 12 phương pháp, sai lệch BPM với Python tối đa 0,05734%. Timer chỉ đo detector, một call/đoạn; workspace không phải tổng RAM firmware.', '',
        'F1 R-peak: ghép một-một trong ±50/150 ms, trung bình đều 22 điều kiện. Chỉ lấy đỉnh trong 5 giây mới của mỗi cửa sổ, chấm từ giây 5 để không đếm lặp. Chấm vị trí QRS do module trả về, không thêm bước hiệu chỉnh vị trí chung.', '',
        'MAE/RMSE tính trực tiếp mọi sai số BPM hợp lệ, không áp ngưỡng bỏ lỗi. ±1% là tiêu chí parity C/ESP32 so với Python, không phải tolerance với BPM tham chiếu.', '',
        '## SQI', '',
        f'BUTQDB: 35.773 cửa sổ 10 giây, 18 record / 15 người. AUC so trên cùng {int(common.sum()):,} cửa sổ hợp lệ ở cả chín phương pháp. Cohort chung loại nhiều cửa sổ xấu không tính được điểm ở bốn method mới; cột AUC mọi điểm hợp lệ giữ phạm vi riêng từng method. Latency host theo phạm vi đo ghi trong bảng.', '',
        '| Phương pháp | AUC cùng cohort | AUC mọi điểm hợp lệ | Coverage % | Host p50 (ms) | Host p99 (ms) | Phạm vi latency |',
        '|---|---:|---:|---:|---:|---:|---|']
    for row in sorted(sqi_rows,key=lambda r:r['auc'],reverse=True):
        lines.append(f"| {names.get(row['method'],row['method'])} | {row['auc']:.4f} | {row['auc_own']:.4f} | {100*row['coverage']:.2f} | {row['p50']:.3f} | {row['p99']:.3f} | {row['scope']} |")
    lines += ['', '* bSQI và hai gate Orphanidou/Zhao dùng primary R đã cache; timer hai gate đo chung, không phải latency riêng từng gate. Bốn SQI NeuroKit mới có coverage lớp tốt 100%, lớp xấu 16,68–17,64%. averageQRS là điểm tương đối trong cửa sổ; dissimilarity không có DTW. SQI chỉ là chỉ số phụ, không chặn AF.', '',
        '## AF', '',
        'AFDB: 6 record ECG1, 21.616 đoạn thuần nhãn 10 giây. So cả tám model trên cùng 18.470 đoạn hợp lệ (85,45%). Model/ngưỡng đóng băng, loại record trùng train cục bộ đã biết.', '',
        '| Model | Sensitivity % | Specificity % | BA % | F1 AF % | AUC |',
        '|---|---:|---:|---:|---:|---:|']
    for row in af.to_dict('records'):
        lines.append(f"| {names[row['method']]} | {100*row['sensitivity']:.2f} | {100*row['specificity']:.2f} | {100*row['ba']:.2f} | {100*row['f1_af']:.2f} | {row['auc']:.4f} |")
    lines += ['', 'ECGFounder: chưa xác minh trùng record với toàn bộ dữ liệu pretraining; không kết luận độc lập hoàn toàn với pretraining.', '']
    lines += ['Fusion LR: train toàn bộ 93 người / 294 session SHDB đã chuẩn bị (5.292 cửa sổ; 5.275 đủ đầu vào), không phải toàn bộ thời lượng record gốc. Chọn ngưỡng 0,239196 bằng OOF 5-fold tách người trên SHDB, rồi fit lại toàn bộ; AFDB chỉ dùng đánh giá. Cùng đúng 18.470 cửa sổ và nhãn thuần của bảng trên. 18.302 cửa sổ chạy fusion, 167 fallback server và 1 fallback device; SQI không hard gate.', '']
    server_table=['### BPM cho server — xử lý toàn record', '',
        'Cùng EDB 57 record, lead 0, 22 điều kiện nhiễu. Các implementation host xử lý toàn record; BPM cập nhật mỗi 5 giây theo giao thức đã chạy. MAE/RMSE và F1 là trung bình đều 22 điều kiện; coverage là thấp nhất. Pan–Tompkins++ dùng bản sửa phát peak lặp. Khác bảng C/ESP32 phía trên ở trạng thái detector và cửa sổ xử lý; không so hai bảng như cùng một cấu hình.', '',
        '| Phương pháp host | MAE (BPM) | RMSE (BPM) | F1 R-peak ±50 ms % | F1 R-peak ±150 ms % | Coverage thấp nhất % |',
        '|---|---:|---:|---:|---:|---:|']
    for r in sorted(server_rows,key=lambda r:r['rmse']):
        server_table.append(f"| {names.get(r['method'],r['method'])} | {r['mae']:.3f} | {r['rmse']:.3f} | {100*r['f1_50']:.2f} | {100*r['f1_150']:.2f} | {100*r['coverage']:.3f} |")
    server_table+=['']
    index=lines.index('## SQI');lines[index:index]=server_table
    text = '\n'.join(lines)
    assert len(lines) < 110
    assert 'N/A' not in text and 'artifact' not in text and 'history' not in text
    for line in lines:
        if line.startswith('|'):
            assert all(cell.strip() for cell in line.split('|')[1:-1])
    if verification is not None:
        aspect,backend,actual=verification
        if aspect=='BPM' and backend=='c':
            expected=bpm.rename(columns={'mean_condition_BPM_MAE':'mae','mean_condition_BPM_RMSE':'rmse',
                'min_condition_coverage':'coverage','F1_50':'f1_50','F1_150':'f1_150'})
            columns=['mae','rmse','coverage','f1_50','f1_150']
        elif aspect=='BPM':
            expected=pd.DataFrame(server_rows);columns=['mae','rmse','coverage','f1_50','f1_150']
        elif aspect=='SQI':
            expected=pd.DataFrame(sqi_rows).rename(columns={'auc':'auc_common','auc_own':'auc_valid'})
            columns=['auc_common','auc_valid','coverage']
        elif aspect=='AFib':
            expected=af.rename(columns={'f1_af':'f1'});columns=['n','sensitivity','specificity','ba','f1','auc']
        else:raise ValueError('Unknown report aspect: '+aspect)
        if set(actual.method)!=set(expected.method):raise ValueError('Incomplete method selection for the report')
        np.testing.assert_allclose(actual.set_index('method').sort_index()[columns],
            expected.set_index('method').sort_index()[columns],rtol=1e-11,atol=1e-12,equal_nan=True,
            err_msg='New metrics differ from the locked report; report not published')
    target = ROOT/'docs/BENCHMARK_SUMMARY_TABLES.md'
    target.write_text(text, encoding='utf-8')
    bpm_section=text[text.index('## BPM'):text.index('## SQI')]
    primary=ROOT/'experiment/report/qrs_rr_bpm/REPORT.md'
    primary.write_text('# Báo cáo QRS / RR / BPM\n\n'+bpm_section+
        '\n## Tái tạo kết quả\n\nXem [lệnh benchmark và tái tạo báo cáo](../../../server/BPM/README.md). '+
        'Các số liệu dùng cache đủ cohort đã audit; host và ESP32 giữ phạm vi đo riêng.\n',encoding='utf-8')
    print(json.dumps(dict(file=str(target), lines=len(lines), BPM=12, BPM_server=len(server_rows), SQI=9, AF=8, source_hashes='PASS'), ensure_ascii=False))
