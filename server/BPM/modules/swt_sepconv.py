"""SWT_SepConv_model: isolated implementation adapter."""
METHOD='SWT_SepConv_model'
CACHE='outputs/BPM/edb_holdout_matrix_20261111'
from server.BPM.modules._runtime import ROOT,load_source

import numpy as np
import torch
def swt_features(ecg, sep_mod):
    """Match the model repository's preprocessing, using the shared AFE waveform."""
    import pywt
    from scipy import signal

    x = np.asarray(ecg, dtype=np.float64).copy()
    coeffs = pywt.wavedec(x, "db3", level=9)
    coeffs[0] = np.zeros_like(coeffs[0])
    x = pywt.waverec(coeffs, "db3")[:len(ecg)]
    sos = signal.butter(5, 40, btype="low", fs=360, output="sos")
    y = signal.sosfilt(sos, x)
    y = signal.sosfilt(sos, y[::-1])[::-1][:len(x)]

    level = 4
    pad = -len(y) % (2 ** level)
    padded = np.pad(y, (0, pad), mode="edge") if pad else y
    coeffs = pywt.swt(padded, "sym4", level=level, trim_approx=True)
    coeffs.reverse()
    wave = coeffs[3][:len(y)]
    rms = np.mean(wave ** 2) ** 0.5
    wave = wave / rms / 16 if rms else wave
    diff = np.diff(y, append=y[-1])
    rms = np.mean(diff ** 2) ** 0.5
    diff = diff / rms / 16 if rms else diff
    return np.stack((wave, diff), axis=1).astype(np.float32)

def infer_swt(ecg, model, localizer_cls, sep_mod):
    feature = swt_features(ecg, sep_mod)
    block = 2048
    outputs = np.empty(len(feature), dtype=np.float32)
    with torch.inference_mode():
        for start in range(0, len(feature), block):
            stop = min(start + block, len(feature))
            chunk = feature[start:stop]
            if len(chunk) < block:
                chunk = np.pad(chunk, ((0, block-len(chunk)), (0, 0)))
            tensor = torch.from_numpy(chunk.T[None, :, :])
            logits = model(tensor).squeeze(0).squeeze(0)
            outputs[start:stop] = torch.sigmoid(logits[:stop-start]).numpy()
    localizer = localizer_cls(np.empty(0, dtype=np.int64), outputs,
                              np.zeros(len(outputs), dtype=np.uint8))
    localizer.find_peak()
    return localizer.list_peak
def load(output=None):
    source=ROOT/'external/ECG_peak_detection'
    sep=load_source(source/'utils/sep_conv.py','_bpm_swt_arch')
    loc=load_source(source/'utils/localizer.py','_bpm_swt_localizer')
    model=sep.Sep_conv_detector(n_channel=2,atrous_rate=[1,3,6,9])
    model.load_state_dict(torch.load(source/'model/trained_model.pt',map_location='cpu',weights_only=True),strict=True)
    model.eval().requires_grad_(False)
    return lambda x:infer_swt(x,model,loc.Localizer,sep)
