"""ECG2RR_LSTM: isolated implementation adapter."""
METHOD='ECG2RR_LSTM'
CACHE='outputs/BPM/edb_holdout_matrix_20261111'
from BPM.modules._runtime import ROOT,load_source

import numpy as np
def load_ecg2rr_model(model_path):
    """Load the repository's legacy H5 model under current Keras.

    The included model was saved before Keras 3 and contains the removed
    `time_major` LSTM setting. Dropping that obsolete setting preserves the
    model's batch-major inference behavior and permits loading its weights.
    """
    from tensorflow import keras
    from BPM.utils.reproducibility import seed_loaded_frameworks
    seed_loaded_frameworks()

    model = keras.Sequential([
        keras.Input(shape=(1000, 1)),
        keras.layers.Bidirectional(keras.layers.LSTM(64, return_sequences=True)),
        keras.layers.Bidirectional(keras.layers.LSTM(64, return_sequences=True)),
        keras.layers.Dense(1, activation="sigmoid"),
    ])
    model.load_weights(model_path)
    return model
def vectorized_means(indices, values):
    """Same sorted groups and float64 averaging as ECG2RR's column_stack path."""
    indices = np.asarray(indices)
    values = np.asarray(values)
    assert indices.shape == values.shape
    if not len(indices):
        return np.empty(0, dtype=np.float64)
    order = np.argsort(indices)
    sorted_indices = indices[order]
    sorted_values = values[order].astype(np.float64, copy=False)
    starts = np.r_[0, np.flatnonzero(np.diff(sorted_indices) != 0)+1]
    counts = np.diff(np.r_[starts, len(indices)])
    return np.add.reduceat(sorted_values, starts)/counts
def load(output=None):
    import sys
    from wfdb import processing
    sys.path.insert(0,str(ROOT/'external/ecg2rr'))
    from ecg2rr import detector
    current=processing.resample_ann
    def nearest(times,indices,*args,**kwargs):
        if args or kwargs:return current(times,indices,*args,**kwargs)
        times=np.asarray(times,float);indices=np.asarray(indices,float)
        right=np.searchsorted(times,indices,side='left').clip(0,len(times)-1);left=(right-1).clip(0,len(times)-1)
        return np.where(indices-times[left]<times[right]-indices,left,right).astype(np.int64)
    processing.resample_ann=nearest;detector.load_model=load_ecg2rr_model
    model=detector.ECG_detector(sampling_rate=360,stride=250,threshold=.05)
    model._calculate_means=vectorized_means
    predict=model.model.predict
    model.model.predict=lambda *a,**kw:predict(*a,**dict(kw,batch_size=32,verbose=0))
    def run(x):
        peaks,probabilities=model.find_peaks(x)
        return model.remove_close(peaks,probabilities,threshold_ms=300)
    return run
