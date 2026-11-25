"""Publish one module independently, after comparison with its audited baseline."""
import json
from pathlib import Path
import numpy as np
import pandas as pd
from SQI.utils._shared import ROOT, checked, save, sha

BPM_HEADERS = {
    'method': 'Phương pháp', 'mae': 'MAE (BPM)', 'rmse': 'RMSE (BPM)',
    'f1_50': 'F1 R-peak ±50 ms %', 'f1_150': 'F1 R-peak ±150 ms %',
    'coverage': 'Coverage thấp nhất %',
}


def audited_csv(folder, name, audit_name, hash_key):
    folder = ROOT / folder
    audit = json.loads((folder / audit_name).read_text(encoding='utf-8'))
    if not audit['status'].startswith('PASS'):
        raise ValueError('Source audit did not pass: ' + str(folder))
    return pd.read_csv(checked(folder / name, audit[hash_key]))


def markdown(frame):
    def fmt(value):
        if isinstance(value, (int, np.integer)):
            return str(value)
        if isinstance(value, (float, np.floating)):
            return f'{value:.4f}'
        return str(value)
    rows = ['| ' + ' | '.join(frame.columns) + ' |',
            '| :--- | ' + ' | '.join(['---:'] * (len(frame.columns) - 1)) + ' |']
    rows += ['| ' + ' | '.join(fmt(v) for v in row) + ' |' for row in frame.itertuples(index=False, name=None)]
    return '\n'.join(rows)


def verify_reference(domain, backend, actual):
    lock = json.loads((ROOT / domain / 'reference.json').read_text(encoding='utf-8'))
    for relative, digest in lock['sha256'].items():
        checked(ROOT / relative, digest)
    name = ('device' if backend == 'c' else 'host') if domain == 'BPM' else domain
    expected = pd.read_csv(ROOT / 'references' / domain / (name + '.csv'))
    if set(actual.method) != set(expected.method) or actual.method.duplicated().any():
        raise ValueError('Incomplete or duplicate methods; report was not published')
    columns = [c for c in expected.columns if c != 'method']
    np.testing.assert_allclose(
        actual.set_index('method').sort_index()[columns],
        expected.set_index('method').sort_index()[columns], rtol=1e-11, atol=1e-12,
        equal_nan=True, err_msg='Reproduced metrics changed from the pre-migration audited baseline')


def refresh(verification, output):
    domain, backend, actual = verification
    output = Path(output)
    verify_reference(domain, backend, actual)
    extra = ''
    if domain == 'BPM':
        table = actual[list(BPM_HEADERS)].copy()
        table[['f1_50', 'f1_150', 'coverage']] *= 100
        if backend == 'c':
            timing = audited_csv('outputs/BPM/esp32_latency_20261117', 'summary.csv',
                                 'module_audit.json', 'summary_sha256')
            if len(timing) != 12 or not timing.cases.eq(66).all() or not timing.pass_cases.eq(66).all():
                raise ValueError('Incomplete ESP32 latency cohort')
            table = table.merge(timing[['method', 'latency_p50_ms', 'workspace_bytes']],
                                on='method', validate='one_to_one')
            if len(table) != len(actual):
                raise ValueError('Missing ESP32 latency method')
            table['workspace_bytes'] /= 1024
            table = table.rename(columns={**BPM_HEADERS, 'latency_p50_ms': 'ESP32 p50 (ms/10s ECG)',
                                          'workspace_bytes': 'Workspace (KiB)'})
            scope = ('C accuracy: EDB 57 record, lead 0; 22 điều kiện; context 10s/hop 5s, '
                     'chỉ chấm QRS trong 5s mới, bắt đầu từ giây 5. '
                     'MAE/RMSE và F1 trung bình đều 22 điều kiện; coverage lấy thấp nhất. '
                     'ESP32 p50 là phép đo detector đã audit trên 66 đoạn 10s, ESP32 160 MHz; '
                     'không gồm AFE, RR/BPM hoặc UART. Workspace là vùng làm việc thuật toán, không phải tổng RAM.')
            section = 'esp32'
        else:
            folder = ROOT / 'visualize/results/BPM/host_rmse_latency'
            manifest = json.loads((folder / 'manifest.json').read_text())
            if manifest['status'] != 'PASS':
                raise ValueError('Host timing audit failed')
            timing = pd.read_csv(checked(folder / 'aggregate.csv', manifest['aggregate_sha256']))
            timing = actual[['method']].merge(timing[['method', 'host_p50_ms']],
                                             on='method', validate='one_to_one')
            if len(timing) != len(actual) or not np.isfinite(timing.host_p50_ms).all():
                raise ValueError('Incomplete Python host latency')
            timing = timing.rename(columns={'method': 'Phương pháp', 'host_p50_ms': 'Host p50 (ms/10s ECG)'})
            timing.to_csv(output / 'latency.csv', index=False)
            extra = '\n\n### Latency trên máy tính\n\n' + markdown(timing)
            table = table.rename(columns=BPM_HEADERS)
            scope = ('Python/host accuracy: EDB 57 record, lead 0; 22 điều kiện; xử lý toàn record. '
                     'MAE/RMSE và F1 trung bình đều 22 điều kiện; coverage lấy thấp nhất. '
                     'Latency host lấy từ phép đo các đoạn 10s đã audit, không phải thời gian xử lý cả record. '
                     'F1 ghép đỉnh một-một trong ±50/150 ms; MAE/RMSE không bỏ lỗi theo tolerance.')
            section = 'python'
    elif domain == 'SQI':
        table = actual[['method', 'auc_common', 'auc_valid', 'coverage']].copy()
        table.coverage *= 100
        table = table.rename(columns={'method':'Phương pháp', 'auc_common':'AUC cùng cohort',
                                      'auc_valid':'AUC mọi điểm hợp lệ', 'coverage':'Coverage %'})
        scope = ('BUTQDB: 35.773 cửa sổ 10s, 18 record/15 người. '
                 f'AUC cùng cohort dùng {int(actual.common_valid.iloc[0]):,} cửa sổ hợp lệ chung. '
                 'Không fit hoặc chọn ngưỡng; SQI không chặn AFib.')
        section = 'SQI'
    elif domain == 'AFib':
        table = actual[['method','sensitivity','specificity','ba','f1','auc']].copy()
        table[['sensitivity','specificity','ba','f1']] *= 100
        table = table.rename(columns={'method':'Phương pháp','sensitivity':'Sensitivity %',
                                      'specificity':'Specificity %','ba':'BA %','f1':'F1 AF %','auc':'AUC'})
        scope = ('AFDB: tám model, cùng 18.470 cửa sổ thuần nhãn 10s. '
                 'Model và ngưỡng đóng băng, không train/tune trên AFDB. '
                 'Fusion LR dùng bản đã train trên 93 người/294 session SHDB chuẩn bị. '
                 'Chưa xác minh disjoint với toàn bộ pretraining ECGFounder.')
        section = 'AFib'
    else:
        raise ValueError('Unknown domain: ' + domain)
    table.to_csv(output / 'metrics.csv', index=False)
    text = f'## {section}\n\n{scope}\n\n{markdown(table)}{extra}\n'
    (output / 'REPORT.md').write_text(text, encoding='utf-8')
    module = ROOT / domain
    latest_path = module / 'results/latest.json'
    latest = json.loads(latest_path.read_text()) if latest_path.exists() else {}
    latest[section] = output.relative_to(ROOT).as_posix()
    report = '# Kết quả ' + domain + '\n\nTái tính từ cache đã audit; không suy luận, train hoặc đo lại thiết bị.\n\n'
    for key in (('python','esp32') if domain == 'BPM' else (domain,)):
        if key in latest:
            report += (ROOT / latest[key] / 'REPORT.md').read_text(encoding='utf-8') + '\n'
    (module / 'RESULTS.md').write_text(report, encoding='utf-8')
    if domain == 'BPM':
        primary = ROOT / 'report/qrs_rr_bpm/REPORT.md'
        primary.parent.mkdir(parents=True, exist_ok=True)
        primary.write_text(report, encoding='utf-8')
    save(latest_path, latest)
    save(output / 'reproduction_audit.json', {'status':'PASS','domain':domain,'backend':backend,
         'methods':len(actual),'baseline_metrics_match':True,'source':str(Path(__file__).relative_to(ROOT)),
         'source_sha256':sha(__file__),'latency':'previously audited measurements, no device run'})
    print('Saved', output / 'metrics.csv')
